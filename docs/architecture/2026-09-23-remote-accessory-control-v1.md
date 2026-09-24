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
describes. This revision covers Tasks 1 to 3: the read-only `amplifier` and
`rfkit` status objects, everything the Core already serves for its
accessories (the `tuner` object, the 4O3A fields, and the TGXL and 4O3A
commands), the Core-owned PGXL (identity before admission, pairing only
after it, the `configurePgxl`, `disconnectPgxl` and
`setPgxlConnectionSettings` commands, and the receive-only refusals of the
amplifier's and tuner's operate controls), the Core-owned RF2K-S (identity
from `/info` before admission, its interface, antenna and tuner rows, the
`configureRfKit`, `disconnectRfKit` and `setRfKitEnabled` commands), band
follow for both amplifiers, and the Core's own TCI server on the station
network (the `stationTci` object and the `setStationTci` command).

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
| `remotePgxlControlVersion` | 11 | 2 | Also: `configurePgxl`, `disconnectPgxl` and `setPgxlConnectionSettings` work, and the Core identifies and pairs the PGXL itself |
| `remoteRfKitControlVersion` | 11 | 1 | The Core mirrors its RF2K-S as the read-only `rfkit` object |
| `remoteRfKitControlVersion` | 11 | 2 | Also: the interface, antenna, tuner and band-follow rows of `rfkit`; `configureRfKit`, `disconnectRfKit` and `setRfKitEnabled` work, and the Core identifies the RF2K-S itself |
| `stationTciVersion` | 11 | 1 | The Core runs its own TCI server on the station network: the read-only `stationTci` object, and `setStationTci` works |

- The Core advertises `remotePgxlControlVersion` and
  `remoteRfKitControlVersion` as 2 and the TGXL and 4O3A versions as 1 when
  it owns its accessories (the headless Core, `nereusd`, always does), and
  all four as 0 otherwise. A Core built between Tasks 1 and 3 says 1 for the
  RF2K-S (and, before Task 2, for the PGXL): the object, no commands. An app
  treats 1 as "readings only" and 2 or more as "readings and commands".
- `stationTciVersion` is 1 on a Core that runs its station TCI server
  (`nereusd` always does; the server itself is on only while the station's
  TCI switch is on), 0 otherwise.
- `remotePgxlControlVersion`, `remoteRfKitControlVersion` and then
  `stationTciVersion` travel last in the minor-11 block of the capabilities
  message, after `hpsdrModel`, `radioProtocol`, `radioAddress` and
  `radioHardwareVersion`, and only to an app that agreed minor 11. An app
  below minor 11 receives exactly the capabilities, objects and deltas it
  received before: none of the three entries, and no `amplifier`, `rfkit` or
  `stationTci` schema, object or delta.
- An app that sees version 0, or no entry, shows no Power Genius or RF-Kit
  readings from this Core and says so (see Window behaviour).
- A later task of this plan adds `accessoryDataVersion` (fault history,
  interlock policy, tune memory). Its section is added here when it is
  built.

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
- PGXL: `disabled`, `disconnected`, `connecting`, `identifying`,
  `retrying`, `connected`, `error`. `identifying` is the Core's identity
  check (a `PowerGeniusXL` discovery announcement from the same address and
  port, and the same serial in the amp's own `info` reply; see "How the
  Core identifies the PGXL"). The PGXL, like the TGXL, never reports
  `discovering`: the discovery listen runs inside `identifying`.
- RF2K-S: `disabled`, `disconnected`, `connecting`, `retrying`,
  `connected`, `error`. `connecting` covers the Core's identity check: a REST
  amp has no separate identifying step, since the `/info` reply that proves
  the address answers is the one that identifies it (see "How the Core
  identifies the RF2K-S").

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
| `deviceModel` | utf8 | The product in the amp's LAN discovery announcement, `PowerGeniusXL`; empty before the identity check. After a refused identity, the product that was found (for example `TunerGenius`) |
| `deviceSerial` | utf8 | The serial in the amp's `info` reply (for example `10-200/24-0046`); empty before |
| `deviceVersion` | utf8 | The version in the amp's connect banner and `info` reply, for example `3.8.9` |
| `deviceNickname` | utf8 | The nickname in the amp's discovery announcement; empty before |
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
| `bandFollow` | enum | Whether the amp follows the radio's band (table in "Band follow"): `off` while not connected, `waiting` until the pairing is answered, `following` once paired |

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

### How the Core identifies the PGXL

A connect banner (`V3.8.9`) says only that the peer speaks the Genius
protocol; a Tuner Genius sends one too, and the amp's `info` reply has no
model. So on every dial, before anything else is sent:

1. The Core opens TCP to the configured address (`connecting`), reads the
   `V` banner and sends only `info` (`identifying`).
2. It listens for LAN discovery on UDP 9008 and 9010 for three seconds and
   takes the announcement whose address and receiving port are the
   connection's. Captured from the real amp:
   `PowerGeniusXL ip=192.168.109.235 v=3.8.9 serial=10-200/24-0046 nickname=PowerGeniusXL`
3. The amp's `info` reply, captured:
   `R<seq>|0|serial=10-200/24-0046  version=3.8.9 protocol=1.0 mains=240`
   (key=value pairs, no leading word, two spaces after the serial).
4. Admitted only when the announced product is exactly `PowerGeniusXL` and
   the two serials are equal. Then, and only then, the Core pairs the amp
   (`amplifier create`, `flexradio ampslice=... ptt=LAN`, `keepalive enable`;
   operator decision of 2026-09-23: pair automatically once the Core has
   confirmed it is a real Power Genius) and follows the band after the amp
   accepts the pairing.

Anything else ends at `error` (and `retrying` when automatic retry is on)
with a reason, having been sent nothing but `info`: another product at the
address, no matching announcement in the window, a serial mismatch, an
`info` reply with no serial or an error code, or no answer within five
seconds. Replacing the address or disconnecting in any phase cancels the
attempt, its discovery listen and any pending retry: the old address is
never dialled again.

## Band follow

`amplifier` and `rfkit` each carry `bandFollow`, one fixed table:

| Value | Name | Meaning |
| --- | --- | --- |
| 0 | `off` | PGXL: not connected. RF2K-S: the TCI server it follows is off |
| 1 | `waiting` | PGXL: connected, not paired yet (or the pairing was refused). RF2K-S: the TCI server is on and the amp is not connected to it; `bandFollowAddress` and `bandFollowPort` say what to enter on the amp |
| 2 | `following` | The amp follows the radio's band |
| 3 | `thisComputerOnly` | RF2K-S: the TCI server accepts only apps on its own computer, so the amp cannot reach it |

The PGXL follows the band once it is paired: the Core sends
`flexradio ampslice=<slice> serial=<radio serial> band=<Hz>` on each band
change (200 ms debounce), and nothing before the pairing reply arrives.

The RF2K-S follows as a TCI app of the Core's station TCI server (see "The
`stationTci` object"): it reads only `vfo:` and `split_enable:`
(2026-05-24-rfkit-rf2ks-applet-design.md section 6.2). `following` means a
TCI app is connected from the amp's configured address (an amp configured
by host name never matches and reads `waiting`; the address line is still
right). While the station's TCI server is on, the Core switches an admitted
amp into TCI mode through its web interface (`PUT /operational-interface`
`{"operational_interface":"TCI"}`), once per connection, and only when the
amp reports another interface; an operator who switches it back on the
amp's panel is not fought. The TCI server's address is entered on the amp's
own touchscreen (the REST call carries no address).

The lines a window shows, in user words: "Band follow: following the
radio"; "Band follow: enter <address>, port <port> as the TCI server on the
amplifier."; "Band follow: off. Turn on the TCI server so the amplifier can
follow the radio."; "Band follow: the TCI server accepts only apps on its
own computer, so the amplifier cannot reach it."; for the PGXL "Band
follow: waiting for the Power Genius to pair with the radio." and "Band
follow: off while the Power Genius is not connected."

## The `rfkit` object (RF2K-S)

Class `RfKitModel`, key `rfkit`, `remoteRfKitControlVersion` 1 (the
properties down to `currentA`) and 2 (the rest). Read-only.

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
| `operationalInterface` | utf8 | `/operational-interface`, as sent: `TCI`, `UDP`, `CAT` or `UNIV`; empty before the first reading |
| `antennaPresentMask` | i64 | `/antennas`: bit N-1 set for each internal antenna N (1 to 4) the amp lists |
| `antennaDisabledMask` | i64 | `/antennas`: bit N-1 set for each internal antenna N the amp lists as `DISABLED` |
| `activeAntennaNumber` | i64 | `/antennas/active` number; 0 before the first reading |
| `activeAntennaExternal` | bool | The active antenna is an external one (no internal antenna is active) |
| `tunerMode` | enum | `/tuner` mode (table below) |
| `tunerSetup` | utf8 | `/tuner` setup, as sent, for example `LC` |
| `tunerInductanceNh` | i64 | `/tuner` L, nH |
| `tunerCapacitancePf` | i64 | `/tuner` C, pF |
| `tunerFrequencyKhz` | i64 | `/tuner` tuned frequency, kHz; 0: not tuned |
| `tunerSegmentKhz` | i64 | `/tuner` segment size, kHz |
| `bandFollow` | enum | "Band follow" table |
| `bandFollowAddress` | utf8 | The TCI server address to enter on the amp while `bandFollow` is `waiting` (or the one it follows); empty otherwise |
| `bandFollowPort` | i64 | That server's port; 0 while the server is off |

`tunerMode`:

| Value | Name | The amp's word |
| --- | --- | --- |
| 0 | `unknown` | None yet, or a word this Core does not know |
| 1 | `bypass` | `BYPASS` |
| 2 | `manual` | `MANUAL` |
| 3 | `autoTuning` | `AUTO_TUNING` (a tune is running) |
| 4 | `auto` | `AUTO` |

### How the Core identifies the RF2K-S

The Core dials `http://<configuredHost>:<configuredPort>/info` and counts
the amp as connected only if the reply's `device` field is `RF2K-S`
(`{"device":"RF2K-S","software_version":{"GUI":200,"controller":267},
"custom_device_name":"KG4VCF"}` on firmware G200C267; design doc
2026-05-24-rfkit-rf2ks-applet-design.md section 6.1, from the amp's
swagger and a live probe). Only then does it poll the other paths. Anything
else is refused with a reason in `connectionError`, recorded as a fault, and
never retried: a different device at the address will not become an
RF2K-S. The `/info` refresh every ten poll cycles checks it again; a device
that stops naming itself `RF2K-S` is dropped the same way. No answer at all
is retried with the connection's backoff (1 s doubling to 60 s) while
automatic retry is on (`RfKit_AutoReconnect`), and otherwise stops at
`error`.

RF2K-S reasons in `rfkit`.`connectionError` (already in user words):

| Reason | When |
| --- | --- |
| "The device at this address is not an RF-Kit RF2K-S amplifier. It reports itself as <device>." | `/info` named another device |
| "The device at this address did not say it is an RF-Kit RF2K-S amplifier." | `/info` named no device |
| "The RF-Kit amplifier did not answer at this address." | No answer and automatic retry off |

## The `stationTci` object and the station TCI server

Class `StationTciModel`, key `stationTci`, `stationTciVersion` 1.
Read-only; the switch changes only through `setStationTci`.

| Property | Kind | Meaning |
| --- | --- | --- |
| `enabled` | bool | The station's TCI switch (kept on the Core) |
| `port` | i64 | The server's TCP port (50001 by default, as the app's own) |
| `listening` | bool | The server accepts connections |
| `stationAddress` | utf8 | The station network address it listens on; empty when it listens only on the Core's own computer |
| `error` | utf8 | Why it could not listen, as the system said; empty otherwise |

The Core runs the app's existing TCI server on its own radio model, so a TCI
app at the station (the RF2K-S first) hears the Core's radio: the init
burst, `vfo:` as the Core's slices move, `split_enable:` (always false:
NereusSDR has no split) and the rest of the protocol a local window's server
speaks. Where it listens:

- the station network: nereusd.conf's `station_tci_bind` when set, else the
  Core's address on the radio's subnet once the radio connects;
- and always the Core's own computer (127.0.0.1), so apps there reach it.
  A bind to every address (`0.0.0.0`) is used as given and covers it.

It transmits for no app until remote transmit: the init burst says
`receive_only:true` and `tx_enable:<rx>,false`; `trx:<rx>,true` touches no
MOX, takes no transmit audio lock and is answered `trx:<rx>,false`; transmit
audio frames are dropped. The Core logs the plain reason "Apps cannot
transmit through the station's TCI server until remote transmit is ready."
and never puts it on the TCI wire.

The TCI compatibility settings (`TciEmulateExpertSDR3Protocol`,
`TciEmulateSunSDR2Pro` and the other `Tci` keys) are not seeded on the
Core: the server reads them from the Core's own settings, where no window
writes them, so it runs on their defaults (the two emulation keys read
True, as in a fresh window).

## The 4O3A and RF-Kit fields on the `radio` object

Class `RadioModel`, key `radio`, sent to every app.

| Property | Kind | Direction | Meaning |
| --- | --- | --- | --- |
| `fourO3AEnabled` | bool | Core to window | The station's 4O3A switch (PGXL, TGXL and the SmartSDR listener) for its radio |
| `fourO3AListening` | bool | Core to window | The Core's SmartSDR listener (TCP 4992) is accepting connections |
| `fourO3AListenerError` | utf8 | Core to window | Why the listener could not start; empty otherwise |
| `rfKitEnabled` | bool | Core to window | The station's RF-Kit switch for its radio. Changed by `setRfKitEnabled`; a raw write is refused (see Refusals) |

## Commands

Each is a `command.invoke` with exactly the arguments shown, in exactly
these wire kinds. The answer is a `command.result`; `accepted: true` means
the Core took the request (see "Accepted is not connected").

| Verb | Arguments | Needs | Effect |
| --- | --- | --- | --- |
| `configureTgxl` | `host` (utf8), `port` (i64, 1 to 65535) | minor 4, `remoteTgxlConfigVersion` 1 | Saves the TGXL address for the Core's radio and starts connecting |
| `disconnectTgxl` | none | minor 4, `remoteTgxlConfigVersion` 1 | Cancels an attempt or closes the connection; the address stays |
| `setFourO3AEnabled` | `enabled` (bool) | minor 4, `remoteFourO3AControlVersion` 1 | Turns the station's 4O3A switch on or off for its radio |
| `configurePgxl` | `host` (utf8), `port` (i64, 1 to 65535) | minor 11, `remotePgxlControlVersion` 2 | Saves the PGXL address for the Core's radio and starts connecting and identifying (see "How the Core identifies the PGXL") |
| `disconnectPgxl` | none | minor 11, `remotePgxlControlVersion` 2 | Cancels an attempt or closes the connection in any phase; nothing is redialled; the address stays |
| `setPgxlConnectionSettings` | `autoReconnect` (bool), `keepaliveSec` (i64, 1 to 3600), `pingSec` (i64, 0 to 3600; 0 is off) | minor 11, `remotePgxlControlVersion` 2 | Saves the three station-wide PGXL connection settings on the Core and applies them to the running connection: automatic retry off drops a pending retry (the phase becomes `disconnected`), a running keepalive takes the new interval, the Core pings the amp every `pingSec` while connected |
| `setRfKitEnabled` | `enabled` (bool) | minor 11, `remoteRfKitControlVersion` 2 | Turns the station's RF-Kit switch on or off for its radio. On with a saved address dials it through the identity check; off stops everything (`disabled`) |
| `configureRfKit` | `host` (utf8), `port` (i64, 1 to 65535) | minor 11, `remoteRfKitControlVersion` 2 | Saves the RF2K-S address for the Core's radio and starts connecting and identifying (see "How the Core identifies the RF2K-S") |
| `disconnectRfKit` | none | minor 11, `remoteRfKitControlVersion` 2 | Cancels an attempt or closes the connection in any phase; nothing is redialled; the address and the switch stay |
| `setStationTci` | `enabled` (bool), `port` (i64, 1024 to 65535) | minor 11, `stationTciVersion` 1 | Saves the station's TCI switch and port on the Core and starts or stops its station TCI server. The Core keeps them across window sessions, other apps connecting and restarts |

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
| `rfKitEnabled` on `radio` (what every app before this contract sent) | "Update this app to turn the RF-Kit amplifier on or off on this Core." The Core's switch stays |
| Any property of `stationTci` | "The Core reports its TCI server here. Turn it on or off with this app's TCI switch." |
| `operate` on `amplifier`, on a receive-only Core (every `nereusd` today) | "Operating the station's amplifier or tuner waits for remote transmit. This station is receive-only." |
| `tuner` telemetry (everything except the three below) | "TunerModel::<name> is hardware telemetry TunerModel only learns from the tuner itself; there is no remote-write path" |
| `tuner` `isOperate`, `isBypass`, `antennaA`, on a receive-only Core | "Operating the station's amplifier or tuner waits for remote transmit. This station is receive-only." Nothing changes on the Core and nothing is sent to the tuner |

Commands:

| Verb | Reason |
| --- | --- |
| `configureTgxl`, `disconnectTgxl` below minor 4 | "Remote TGXL configuration requires a newer station protocol." |
| `configurePgxl`, `disconnectPgxl`, `setPgxlConnectionSettings` below minor 11 | "Update this app to set up the Power Genius on this Core." |
| `configurePgxl` with other arguments | "invalid host or port argument" |
| `disconnectPgxl` with arguments | "disconnectPgxl takes no arguments" |
| `setPgxlConnectionSettings` with other arguments | "setPgxlConnectionSettings requires an autoReconnect boolean and keepaliveSec and pingSec whole numbers" |
| `configurePgxl`, `disconnectPgxl`, `setPgxlConnectionSettings` on a Core that does not own its accessories | "Station accessory configuration is unavailable." |
| `configurePgxl` with no radio | "Connect Core to a radio before configuring its PGXL." |
| `configurePgxl` with 4O3A off | "Enable 4O3A on Core before connecting the PGXL." |
| `configurePgxl` with a bad address | "Enter a valid PGXL IP address or hostname and TCP port 1 to 65535." |
| `setPgxlConnectionSettings` out of range | "Enter a keepalive of 1 to 3600 seconds and a ping of 0 to 3600 seconds." |
| `configureRfKit`, `disconnectRfKit`, `setRfKitEnabled` below minor 11 | "Update this app to set up the RF-Kit amplifier on this Core." |
| `configureRfKit` with other arguments | "invalid host or port argument" |
| `disconnectRfKit` with arguments | "The disconnect request for the RF-Kit amplifier was not understood." |
| `setRfKitEnabled` with other arguments | "The request to turn the RF-Kit amplifier on or off was not understood." |
| `configureRfKit`, `disconnectRfKit`, `setRfKitEnabled` on a Core that does not own its accessories | "Station accessory configuration is unavailable." |
| `configureRfKit` with no radio | "Connect the Core to a radio before setting up its RF-Kit amplifier." |
| `setRfKitEnabled` with no radio | "Connect the Core to a radio before turning its RF-Kit amplifier on or off." |
| `configureRfKit` with the RF-Kit switch off | "Turn on the RF-Kit amplifier on the Core before connecting it." |
| `configureRfKit` with a bad address | "Enter the RF-Kit amplifier's IP address or host name, and a port from 1 to 65535." |
| `setStationTci` below minor 11 | "Update this app to turn the station's TCI server on or off." |
| `setStationTci` on a Core without a station TCI server | "This Core has no TCI server for the station." |
| `setStationTci` with other arguments | "The request to turn the station's TCI server on or off was not understood." |
| `setStationTci` with a port outside 1024 to 65535 | "Choose a TCI port from 1024 to 65535." |
| `setFourO3AEnabled` below minor 4 | "Remote 4O3A control requires a newer station protocol." |
| `configureTgxl` with other arguments | "invalid host or port argument" |
| `disconnectTgxl` with arguments | "disconnectTgxl takes no arguments" |
| `setFourO3AEnabled` with other arguments | "setFourO3AEnabled requires exactly one enabled boolean argument" |
| `configureTgxl`, `disconnectTgxl` on a Core that does not own its accessories | "Station accessory configuration is unavailable." |
| `configureTgxl` with no radio | "Connect Core to a radio before configuring its TGXL." |
| `configureTgxl` with 4O3A off | "Enable 4O3A on Core before connecting the TGXL." |
| `configureTgxl` with a bad address | "Enter a valid TGXL IP address or hostname and TCP port 1–65535." |
| `setFourO3AEnabled` with no radio | "Connect Core to a radio before changing its 4O3A integration." |
| Any refusal without its own reason | "TGXL configuration was refused", "TGXL disconnect was refused", "4O3A master change was refused", "PGXL configuration was refused", "PGXL disconnect was refused", "PGXL settings change was refused", "The Core did not set up the RF-Kit amplifier.", "The Core did not disconnect the RF-Kit amplifier.", "The Core did not change its RF-Kit amplifier switch.", "The Core did not change its TCI server." |

The desktop app does not send a command its Core did not offer. It shows
"The station does not support remote TGXL configuration.", "The station
does not support remote PGXL configuration.", "The station does not
support remote 4O3A control.", "This Core does not offer RF-Kit amplifier
setup to this app." or "This Core has no TCI server for the station."
instead.

PGXL identity reasons in `amplifier`.`connectionError` (diagnostic text; an
app shows them in its own words):

| Reason | When |
| --- | --- |
| "Expected PowerGeniusXL at the connected endpoint; observed <product> (serial <serial>)." | The announcement at the address names another product |
| "No matching PGXL discovery announcement for <address>:<port>. Check the amplifier address, port and station LAN discovery." | No announcement from the address in the three-second window |
| "PGXL identity serial mismatch: expected <announced>, observed <info>" | The two serials differ |
| "PGXL native info failed with code <hex>" | The `info` reply carried an error code |
| "PGXL native info omitted a nonempty serial" | The `info` reply had no serial |
| "PGXL native identity timed out" | No `info` reply within five seconds |
| "PGXL discovery approval timed out for serial <serial>" | The `info` reply came, the matching announcement did not, within five seconds |

## Core-owned settings

The Core keeps every accessory setting. A window changes one only through a
command above (or, for the settings not yet behind a command, not at all
from a remote window). Keys are the Core's `NereusSDR.settings` keys.

Per radio, under `hardware/<mac>/peripherals/`:

| Key | Value | Changed by |
| --- | --- | --- |
| `FourO3A_Enabled` | `True` / `False` | `setFourO3AEnabled` |
| `TGXL_ManualIp`, `TGXL_ManualPort` | text, whole number | `configureTgxl` |
| `PGXL_ManualIp`, `PGXL_ManualPort` | text, whole number | `configurePgxl` |
| `RfKit_Enabled` | `True` / `False` | `setRfKitEnabled` |
| `RfKit_ManualIp`, `RfKit_ManualPort` | text, whole number | `configureRfKit` |

Station-wide, behind `setStationTci`: `StationTci_Enabled` (`True` /
`False`, default `False`) and `StationTci_Port` (default 50001). Where the
server listens on the station network is `station_tci_bind` in
`nereusd.conf` (empty: the Core's address on the radio's subnet).

Station-wide, behind `setPgxlConnectionSettings`: `PGXL_AutoReconnect`
(`True` / `False`, default `True`), `PGXL_KeepaliveSec` (default 30),
`PGXL_PingSec` (default 0 on the Core, off: the amp's reply to `ping` has
never been captured).

Station-wide, not yet behind a command: `PGXL_BroadcastDiscovery`, `PGXL_BroadcastNickname`,
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

- PGXL OPERATE and STANDBY, and PGXL standby around a TGXL tune. A write of
  `amplifier` `operate` is refused with the receive-only reason above.
- TGXL TUNE (autotune), operate, bypass, antenna choice, relay nudges, and
  tune-memory recall on a band change.
- RF2K-S OPERATE and STANDBY, antenna choice, error reset. (The Core does
  put an admitted RF2K-S into TCI mode while the station's TCI server is on,
  once per connection; that chooses where the amp reads the radio's
  frequency from and keys nothing.)
- Transmit over the Core's station TCI server (see "The `stationTci`
  object").
- Enforcement of the interlock policy, and the MOX RF-flow gate (these stay
  on the Core; only viewing and changing the policy comes before remote
  transmit, in Task 4).

A `property.write` of `tuner` `isOperate`, `isBypass` or `antennaA` is
refused on a receive-only Core with the receive-only reason, before it can
reach the tuner (until Task 2 it reached `TunerModel::applyMirroredValue`, an
R2 path, whatever the policy).

Pairing is not operating: the Core pairs an admitted PGXL
(`flexradio ... ptt=LAN`) automatically, by operator decision of
2026-09-23. It keys nothing; the Core's own transmit refusals still apply.

A remote window shows these controls disabled: the Power Genius tab's
Operate button with the receive-only reason, the applets' OPERATE and
antenna buttons with "Amplifier control is not available from a remote
window yet." (Power Genius and RF-Kit) or their transmit-permission reason
(Tuner Genius), and the RF-Kit page's "Set amp to TCI mode" and "Reset amp
error state" buttons.

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
- With `remotePgxlControlVersion` 2, the desktop remote window's
  Peripherals page Power Genius row and its 4O3A Power Genius tab ask the
  Core (`configurePgxl`, `disconnectPgxl`, `setPgxlConnectionSettings`) and
  show the Core's `amplifier` phase, identity and reason. The button says
  Connect, Cancel (while `connecting`, `identifying` or `retrying`) or
  Disconnect. LAN scanning is not offered remotely. Below 2 the row and tab
  say "This Core does not offer Power Genius XL control to this app." A
  local window keeps its own connection and its existing row.
- With `remoteRfKitControlVersion` 2, the desktop remote window's RF-Kit
  page (CAT & Network > RF-Kit) asks the Core: its switch sends
  `setRfKitEnabled` and follows the Core's `rfKitEnabled`; Connect sends
  `configureRfKit` with the page's address; Disconnect sends
  `disconnectRfKit`; the status line is the Core's `rfkit` phase and
  identity. The RF-Kit applet's Disconnect or Reconnect asks the Core the
  same way. Below 2 the page says "This Core does not offer RF-Kit
  amplifier setup to this app." and changes nothing. The RF-Kit settings the
  Core keeps (automatic retry, poll interval, antenna labels) are shown and
  not changed from a remote window.
- The amp page and applet show the band-follow line (see "Band follow"):
  the RF-Kit page and applet from `rfkit`, the Power Genius applet and the
  4O3A page's General tab from `amplifier`, in local and remote windows.
- The app's one TCI switch and port (CAT & Network > TCI Server) start this
  window's own TCI server as before and, with `stationTciVersion` 1, send
  `setStationTci` so the Core's station server runs on the same port; off
  stops both. The command is sent only when the operator changes the switch
  or the port; a window connecting does not change the Core's switch. The
  page adds "Also at the station: <address>, port <port>" while the Core's
  server listens ("The station's TCI server is not running." while it is on
  and not listening). When the Core runs on the same computer as the
  window, the window runs no server of its own and apps there use the
  Core's; the page then says "The Core on this computer serves TCI apps
  here, port <port>."
- A local window's RF-Kit band follow is worked out from its own TCI server
  the same way.

## Fixtures

`tests/fixtures/accessories/` holds files a client's conformance suite can
load:

- `amplifier.jsonl` and `rfkit.jsonl`: one session message per line, as the
  Core sends them: the `schema`, the `object.create` after the captured
  readings (PGXL `R1|0|state=OPERATE temp=42.5 vac=240 fwd=1480.0 swr=2.1`
  and the setup reply with `meffa=off`; RF2K-S `/info`, `/power` and
  `/operate-mode` replies, with `/operational-interface`, `/antennas` and
  `/antennas/active`), `snapshot.complete`, then deltas (PGXL transmitting
  at 60 dBm with -24.5 dB return loss, then standby, then band follow
  `following`; RF2K-S standby with an idle `/power`, then a `/tuner`
  reading, TCI mode, antenna 2 and band follow `waiting` with an address).
- `enums.json`: the `connectionPhase`, `amplifierState`, `bandFollow` and
  `rfkitTunerMode` tables and the three read-only refusal texts.

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
- `tst_station_pgxl_controller`: the captured discovery and `info` reply
  admit a PGXL, which is then paired and follows the band; a Tuner Genius,
  a serial mismatch and a silent peer are never admitted and are sent only
  `info`; cancelling or switching off in every phase never redials; A
  replaced by B while A is identifying leaves only B; the connection
  settings are saved and applied; a radio's saved address is dialled
  through the identity check.
- `tst_pgxl_connection_reconnect`: the owned retry timer (replaced or
  cancelled addresses never redialled, a fresh socket per retry, a late
  callback from a replaced attempt cannot act, automatic retry off drops a
  pending retry).
- `tst_remote_peripherals`: the remote Power Genius row and tab through the
  link, end to end through the Core over the loopback, and the receive-only
  refusals of the tuner's and amplifier's operate controls (nothing
  changes, nothing is sent to the tuner).

- `tst_station_rfkit_controller`: an RF2K-S naming itself in `/info` is
  admitted and only then connected; another device, or none, is refused,
  recorded as a fault and never retried; no answer retries (or says why with
  retry off); cancelling while identifying never admits; the Core puts the
  amp in TCI mode once per connection while the station's TCI server is on,
  not otherwise and not when it already is; the Core's configure and switch
  rules; a radio's saved address dialled through the identity check.
- `tst_rf2ks_connection_lifecycle`, `tst_rf2ks_connection_parse`: identity
  admission on the wire, a device changing mid-session, the local default
  unchanged, the retry and failure reports, the reported device and the
  interface faults.
- `tst_rfkit_radiomodel_enabled`, `tst_rfkit_page_master_gate`: the switch
  has no raw write, a remote window holds the Core's value and never
  switches it itself, the Core runs it through its controller; the remote
  page asks the Core and dials nothing; the band-follow line.
- `tst_station_tci_server`: where the Core listens (the station network
  facing the radio, the override, this computer), the switch kept on the
  Core across restarts, a TCI app at the station hearing receive-only,
  `split_enable:` and `vfo:` as the Core's slice moves, transmit refused,
  the RF-Kit's band follow over the server, the one switch driving both
  servers (none of the window's own when the Core is on this computer), and
  the TCI page's line.
- `tst_tci_tx_mutex`: the station server's transmit refusal on the wire.
- `tst_remote_peripherals`: the RF-Kit set up, switched and disconnected
  from a remote window through the Core over the loopback, its rows reaching
  the applet, a raw `rfKitEnabled` write refused, the Power Genius's
  band-follow line local and remote, and the one TCI switch turning the
  Core's station server on and off, which keeps running when the window
  goes and another connects.

Hardware evidence is pending for the operator checkpoint: readings from the
real PGXL and RF2K-S reaching a remote window and the iPhone app; identity
and pairing with the real PGXL on the Core (the discovery announcement and
`info` reply used here are the real amp's, captured on 2026-05-19 and
2026-05-20 in `captures/`); the real RF2K-S admitted by its `/info`, put in
TCI mode by the Core, and following band changes made from the remote
window and from the iPhone app through the Core's station TCI server (the
RF2K-S pointed at the Rock); the station server bound on the Rock's station
network.
