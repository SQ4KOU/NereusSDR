# Station link version 1

This is the written specification of the link between a NereusSDR station
(the Core, `nereusd`) and a client (the desktop remote window, the iPhone
app). It is requirement R-IOS-01 of the
[iPhone app plan](2026-09-23-iphone-app-plan.md). Both clients implement
from this document, and the conformance suite in `tests/data/link/v1/`
holds both of them to it.

Every value here is a claim about the station's code, read from the source
named beside it. The tables are not written by hand:
`scripts/render-link-tables.py` renders them from
`tests/data/link/v1/surface.json`, which `tst_link_surface_manifest`
captures from the code and guards against drift (section 16). CI runs
`python3 scripts/render-link-tables.py --check` in the compliance job, so a
document that no longer matches the surface fails the build.

Section numbers are stable. Later revisions add sections; they never
renumber.

## 1. The link and its normative parts

The link is one control connection per client: a TLS WebSocket carrying
JSON messages. Media (display rows and audio) travels on a separate
encrypted media connection that the control connection negotiates.

This document states the connection-level rules itself (transport,
identity, the connect sequence, versions, the envelope of every message,
liveness and ending) and indexes the parts that are specified in their own
files. Each of those files is normative for its part and is still
maintained there:

| Part | Document |
| --- | --- |
| Media control (`media.control` operations, the media peer, display subscriptions, receiver audio, the headphones mix, the audio clock) | [2026-09-20-remote-media-control-v1.md](2026-09-20-remote-media-control-v1.md) |
| Display codec (the NSDC v1 frames on the media connection) | [2026-09-20-display-codec-v1.md](2026-09-20-display-codec-v1.md) |
| Notch control (the `notches` object and the `notch.*` commands) | [2026-09-23-remote-notch-control-v1.md](2026-09-23-remote-notch-control-v1.md) |
| Accessory control (the `tuner`, `amplifier`, `rfkit` and `stationTci` objects, the 4O3A, RF-Kit and station TCI commands and refusals) | [2026-09-23-remote-accessory-control-v1.md](2026-09-23-remote-accessory-control-v1.md) |

The design authority behind all of them is the
[remote daemon architecture design](2026-07-28-remote-daemon-architecture-design.md),
the [station identity and pairing design](2026-08-02-remote-station-identity-and-pairing-design.md)
and the [R3 plan](2026-09-20-remote-daemon-r3-plan.md).

## 2. Transport

- The station listens with a `QWebSocketServer` in secure mode
  (`StationServer.cpp`, `listen()`), so every connection is a WebSocket
  over TLS (`wss://`). The server takes Qt's default TLS configuration
  (`QSslConfiguration::defaultConfiguration()`), whose protocol setting is
  `QSsl::SecureProtocols`: TLS 1.2 or later. The station does not ask for
  a client certificate (`setPeerVerifyMode(QSslSocket::VerifyNone)`); the
  client's proof of identity is the token (section 3.3).
- The control port is set by `remote_port` in `nereusd.conf`
  (`DaemonConfig.h`). Its default, 0, means the station does not listen.
- Messages travel as WebSocket text frames. Each text message is one JSON
  object, encoded compactly, with a string `type` key naming its kind
  (`SessionMessages::encode`). The station ignores binary frames: only
  `textMessageReceived` is connected (`SessionTransport.cpp`).
- Heartbeats are WebSocket ping and pong control frames, not JSON messages
  (section 12.1). The ping payload is the constant `nereus` and is never
  inspected.
- The station closes a connection with close code 1000 (normal) and a
  reason text; the reason a client acts on always arrives first in a
  `session.end` or `auth.result` message (section 12.4).

## 3. Station identity

### 3.1 The certificate

The station makes its own certificate the first time it runs and keeps it
(`CertificateStore.cpp`):

- self-signed, subject and issuer common name `nereusd`;
- an RSA key of 3072 bits (`kRsaKeyBits`), signed with SHA-256
  (`X509_sign(..., EVP_sha256())`);
- valid from one day before creation (`kNotBeforeSkewSeconds`) for
  `60 * 60 * 24 * 365 * 10` seconds, which is 3650 days
  (`kValiditySeconds`, "10 years");
- a random positive 63-bit serial number.

There is no renewal. A new certificate changes the pin, and every paired
client has to pair again.

### 3.2 The pin

The pin is the SHA-256 digest of the whole certificate in DER form,
written as 32 uppercase hexadecimal byte pairs joined by colons: 95
characters, for example `AB:12:...:FF` (`CertificateStore::fingerprintSha256()`,
`formatFingerprint` in both `CertificateStore.cpp` and `StationClient.cpp`).
It is what `openssl x509 -fingerprint -sha256` prints without its label.

A client compares the pin with the certificate the station presents before
it sends its token. The desktop client compares case-insensitively (it
upper-cases the pin it holds, `StationClient.cpp`), refuses to dial with
no pin unless its operator explicitly allowed an unpinned link, and refuses
a pinned link whose address carries no TLS. The station prints the pin with the
token at first run; the LAN announcement carries it too (section 14).

### 3.3 The token

- 32 random bytes (256 bits), written as base64url without padding: 43
  characters (`TokenStore.cpp`, `kTokenBytes`).
- One token per station, kept in the station's profile directory.
- After 5 consecutive failed checks (`kDefaultMaxFailuresPerLockout`) the
  station refuses every check, including a correct token, for 60 s
  (`kDefaultLockoutMs` 60000). The lockout is global, not per peer.
- The station never logs a candidate token.

## 4. Messages

Every message is `{"type": "<kind>", ...}`. The table lists every kind, the
keys a message of that kind must carry (removing one makes the station's
decoder refuse the message) and the keys it may carry.

<!-- surface:messageKinds -->
<!-- Generated by scripts/render-link-tables.py from tests/data/link/v1/surface.json. Do not edit by hand. -->

| Kind (`type`) | Required keys | Optional keys |
| --- | --- | --- |
| `auth.request` | `token`, `type` | none |
| `auth.result` | `accepted`, `reason`, `type` | `retryable` |
| `capabilities` | `properties`, `type` | none |
| `command.invoke` | `args`, `id`, `type`, `verb` | none |
| `command.result` | `accepted`, `affected`, `id`, `reason`, `type`, `verb` | `values` |
| `delta` | `key`, `properties`, `type` | none |
| `hello` | `major`, `minor`, `peer`, `settingsSchema`, `type` | none |
| `media.control` | `payload`, `type` | none |
| `object.create` | `class`, `key`, `properties`, `type` | none |
| `object.destroy` | `class`, `key`, `type` | none |
| `property.result` | `key`, `results`, `type`, `writeId` | none |
| `property.write` | `key`, `properties`, `type` | `writeId` |
| `schema` | `class`, `fields`, `type` | none |
| `session.end` | `reason`, `type` | `retryable` |
| `settings.reject` | `key`, `properties`, `type` | `reason` |
| `settings.remove` | `key`, `properties`, `type` | none |
| `settings.snapshot` | `properties`, `type` | none |
| `settings.value` | `key`, `origin`, `properties`, `type` | none |
| `settings.write` | `key`, `origin`, `properties`, `type` | none |
| `snapshot.complete` | `type` | none |
| `station.metrics.v1` | `payload`, `type` | none |

<!-- /surface -->

### 4.1 Property entries

Capabilities, settings, object properties, deltas, command arguments and
command result values all carry the same entry:

```json
{"ordinal": 0, "name": "frequency", "kind": "f64", "value": 14074000.0}
```

`kind` is one of `bool`, `i64`, `f64`, `utf8` or `enum`
(`kWireKindNames`, `SessionMessages.cpp`). An `i64` or `enum` value is a
JSON number that must be a whole number inside the 64-bit signed range;
JSON numbers are doubles, so values are exact up to 2^53. A `utf8` value is
a string and a `bool` value is `true` or `false`. No value is coerced from
another JSON type: a mismatch refuses the whole message. `ordinal` is the
property's position in its class's schema (section 7); capabilities,
settings and command arguments carry 0.

### 4.2 Numbers that are not finite

JSON has no NaN and no infinity. An `f64` value that is not finite travels
as one of three strings: `"nan"`, `"inf"` or `"-inf"` (`kFloatNan`,
`kFloatPosInf`, `kFloatNegInf` in `SessionMessages.cpp`). Every other
string in an `f64` entry is refused. The case is common: `SliceModel`'s
`snrDb` is NaN until a RADE signal is decoded.

## 5. Connecting

### 5.1 The connect sequence

1. The client opens the WebSocket and checks the pin (section 3.2).
2. The station sends `hello` first, as soon as it accepts the connection,
   before the client has sent anything (`StationServer::acceptTransport`).
   A client can therefore refuse an incompatible major without having sent
   its token.
3. The client answers with its own `hello`, then `auth.request` carrying
   the token (`StationClient.cpp`, after its pin check).
4. The station sends `auth.result`. On success, in this order
   (`StationServer::promoteToSession`):
   - `capabilities` (section 6);
   - `settings.snapshot`: every station-scoped setting (section 8);
   - one `schema` per mirrored class, in the order the classes are first
     watched (`StateMirror::attachSession`);
   - one `object.create` per object, each with its full property set: the
     fixed objects first, then one per slice;
   - `snapshot.complete`.
5. From then on the station sends `delta`, `object.create` and
   `object.destroy` as its state changes, and the client may send property
   writes, settings writes and commands.

A `hello` carries `major`, `minor`, `settingsSchema` (the sender's settings
schema version) and `peer` (a name for the sending program). A difference
in `settingsSchema` is logged by both ends and is not a refusal.

The station refuses, with `session.end` and `retryable` false: a second
`hello` ("duplicate hello"), `auth.request` before `hello` ("auth before
hello"), a second `auth.request` ("duplicate auth"), and any other message
before authentication ("message sent before authentication").

### 5.2 Resends during a session

- **A late radio.** A station can accept a client before its radio is
  found. When the radio connects, the station sends `capabilities` and
  `settings.snapshot` again, one event-loop turn later, to the session
  that was current when the radio arrived (`currentRadioChanged` in the
  `StationServer` constructor). Settings are scoped to the connected radio,
  so the snapshot is merged into the client's settings, not a replacement.
- **A changed display allowance.** When the station's display budget
  changes, it sends `capabilities` again with the new allowance
  (`publishDisplayBudgetCapabilities`), when media is available to the
  session. It does not resend `settings.snapshot` in this case.

A client treats every `capabilities` message as a whole new set.

## 6. Versions and capabilities

### 6.1 The version rule

Both ends send a major and a minor in `hello`: `kSessionProtocolMajor` 1
and `kSessionProtocolMinor` 11 (`SessionMessages.h`).

- A different major is refused. The station ends the connection with
  `session.end`, `retryable` false, naming both versions; the desktop
  client applies the same rule to the station's `hello` and disconnects.
- An equal major agrees the lower of the two minors
  (`std::min(kSessionProtocolMinor, message.protocolMinor)`), and each end
  keeps to what the agreed minor allows.

### 6.2 The two-key feature gate

A feature is available only when both keys hold: the agreed minor is at
least the minor the feature arrived in, **and** the station advertises the
feature's capability version at a value at least the version the feature
needs (at least 1 for its first version). The desktop client applies the
gate in one place per feature; for example `remoteRfKitControlAvailable()`
is `agreedMinor >= kRadioIdentitySessionProtocolMinor` (11) and
`remoteRfKitControlVersion >= 2` (`StationClient.cpp`). The station applies
the minor half again on its side and refuses a gated command from an older
peer with a plain reason (section 9.3).

Two gates in the table of section 9 need more than one row can say:

- The PureSignal action verbs (`ps3.off`, `ps3.single`, `ps3.automatic`,
  `ps3.applyCurrent`, `ps3.twoTone`, `ps3.saveCorrection`,
  `ps3.restoreCorrection`) need `psAlgorithmVersion` **equal to** 3, not at
  least 3 (`StationClient.cpp`, `setRemoteCapabilities(... psAlgorithmVersion == 3 ...)`).
- `nnr.applyModelSelection` needs both `nnrVersion` at least 1 and
  `dspAssetVersion` at least 1 (`requestApplyNnrModels`). The table records
  only `dspAssetVersion`.

### 6.3 Per-feature capability versions

The minor stops at 11. A feature added since then gets its own
`<feature>Version` capability instead of a new minor, and later revisions
of that feature raise its version. The table gives the value a station
sends with every feature switched on: media, telemetry, an enforced
display budget, the Core owning its accessories, a station TCI server, the
step attenuator, the Alex antennas and the HL2 I/O board, and a peer at
agreed minor 11 (`StationServer::buildCapabilities`). It is generated from
the `value` of each entry of `surface.json`'s `capabilities` list, which
`tst_link_surface_manifest` captures from a live station, so a version
change shows as surface drift and as a change to this table.

<!-- surface:capabilityVersions -->
<!-- Generated by scripts/render-link-tables.py from tests/data/link/v1/surface.json. Do not edit by hand. -->

| Capability | Value with every feature on |
| --- | --- |
| `txPermitted` | false |
| `remoteMediaVersion` | 1 |
| `remoteWidebandDisplayVersion` | 1 |
| `remoteAudioStatusVersion` | 1 |
| `spectrumGrantVersion` | 1 |
| `remoteDisplayBudgetVersion` | 1 |
| `remoteCtunVersion` | 1 |
| `stationTelemetryVersion` | 3 |
| `remoteTgxlConfigVersion` | 1 |
| `remoteFourO3AControlVersion` | 1 |
| `wdspVersion` | 210 |
| `wdspCompatibilityVersion` | 1 |
| `nnrVersion` | 1 |
| `psAlgorithmVersion` | 3 |
| `propertyResultVersion` | 1 |
| `dspAssetVersion` | 2 |
| `psDisplayVersion` | 1 |
| `notchControlVersion` | 1 |
| `audioProfileVersion` | 1 |
| `audioClockVersion` | 1 |
| `receiverAudioVersion` | 1 |
| `headphonesMixVersion` | 1 |
| `radioHardwareVersion` | 3 |
| `remotePgxlControlVersion` | 2 |
| `remoteRfKitControlVersion` | 2 |
| `stationTciVersion` | 1 |

<!-- /surface -->

When a feature is off, its version is 0:

- `remoteMediaVersion`, `remoteWidebandDisplayVersion`,
  `remoteAudioStatusVersion`, `spectrumGrantVersion`, `audioProfileVersion`,
  `audioClockVersion`, `receiverAudioVersion`, `headphonesMixVersion`: 0
  unless media is enabled.
- `remoteDisplayBudgetVersion`: 0 unless media and budget enforcement are
  on and a budget has been computed.
- `stationTelemetryVersion`: 0 unless telemetry is enabled.
- `remoteTgxlConfigVersion`, `remoteFourO3AControlVersion`: 0 unless the
  Core owns its accessories.
- `wdspVersion`, `wdspCompatibilityVersion`, `nnrVersion`,
  `psAlgorithmVersion`, `dspAssetVersion`: 0 on a station built without
  WDSP. `psDisplayVersion` also needs media.
- `remoteCtunVersion`, `propertyResultVersion` and `notchControlVersion`
  are never 0.
- `radioHardwareVersion`: sent only at agreed minor 11. 0 without the step
  attenuator bound; 1 with it; 2 with the Alex antennas too; 3 with the HL2
  I/O board too.
- `remotePgxlControlVersion`, `remoteRfKitControlVersion`: sent only at
  agreed minor 11, and 0 unless the Core owns its accessories.
- `stationTciVersion`: sent only at agreed minor 11, and 0 unless the Core
  runs a station TCI server.

`txPermitted` is always false today: remote transmit is R4.

### 6.4 The capabilities message

`capabilities` carries property entries (section 4.1) in the order below.
The display budget entries (`displayApplicationBytesPerSecond`,
`spectrumSampleUnitsPerSecond`, `displayBudgetGeneration`,
`remotePs3DisplaySubscribed`, `displayBudgetReason`) are present only with a
usable budget, and `displayBudgetReason` only at agreed minor 11. The radio
identity entries from `hpsdrModel` onwards are present only at agreed minor
11. A client ignores a capability it does not know
(`StationCapabilities::fromUpdates`).

<!-- surface:capabilities -->
<!-- Generated by scripts/render-link-tables.py from tests/data/link/v1/surface.json. Do not edit by hand. -->

| Order | Name | Wire kind |
| --- | --- | --- |
| 1 | `stationName` | `utf8` |
| 2 | `radioModel` | `utf8` |
| 3 | `firmwareVersion` | `utf8` |
| 4 | `macAddress` | `utf8` |
| 5 | `board` | `i64` |
| 6 | `radioConnected` | `bool` |
| 7 | `effectiveMaxSlices` | `i64` |
| 8 | `boardMaxSlices` | `i64` |
| 9 | `userDdcCount` | `i64` |
| 10 | `pureSignalPresent` | `bool` |
| 11 | `txPermitted` | `bool` |
| 12 | `remoteMediaVersion` | `i64` |
| 13 | `remoteWidebandDisplayVersion` | `i64` |
| 14 | `remoteAudioStatusVersion` | `i64` |
| 15 | `spectrumGrantVersion` | `i64` |
| 16 | `remoteDisplayBudgetVersion` | `i64` |
| 17 | `remoteCtunVersion` | `i64` |
| 18 | `stationTelemetryVersion` | `i64` |
| 19 | `remoteTgxlConfigVersion` | `i64` |
| 20 | `remoteFourO3AControlVersion` | `i64` |
| 21 | `wdspVersion` | `i64` |
| 22 | `wdspCompatibilityVersion` | `i64` |
| 23 | `nnrVersion` | `i64` |
| 24 | `psAlgorithmVersion` | `i64` |
| 25 | `propertyResultVersion` | `i64` |
| 26 | `dspAssetVersion` | `i64` |
| 27 | `psDisplayVersion` | `i64` |
| 28 | `notchControlVersion` | `i64` |
| 29 | `audioProfileVersion` | `i64` |
| 30 | `audioClockVersion` | `i64` |
| 31 | `receiverAudioVersion` | `i64` |
| 32 | `headphonesMixVersion` | `i64` |
| 33 | `settingsSchemaVersion` | `i64` |
| 34 | `displayApplicationBytesPerSecond` | `i64` |
| 35 | `spectrumSampleUnitsPerSecond` | `i64` |
| 36 | `displayBudgetGeneration` | `i64` |
| 37 | `remotePs3DisplaySubscribed` | `bool` |
| 38 | `displayBudgetReason` | `utf8` |
| 39 | `hpsdrModel` | `i64` |
| 40 | `radioProtocol` | `i64` |
| 41 | `radioAddress` | `utf8` |
| 42 | `radioHardwareVersion` | `i64` |
| 43 | `remotePgxlControlVersion` | `i64` |
| 44 | `remoteRfKitControlVersion` | `i64` |
| 45 | `stationTciVersion` | `i64` |

<!-- /surface -->

## 7. Objects and properties

### 7.1 Classes, schemas and keys

A `schema` message names a class and its fields: each field's ordinal, name
and wire kind. The station mirrors these classes; a property's direction is
`outbound` (station to client only; a write is refused), `bidirectional`
(the client may write it) or `constantSnapshot` (sent in `object.create`
only, never in a delta, never written; `SliceModel`'s `sliceIndex` is one).
An enum property lists the values its domain allows.

<!-- surface:mirrorClasses -->
<!-- Generated by scripts/render-link-tables.py from tests/data/link/v1/surface.json. Do not edit by hand. -->

**AlexAntennaFacade** (10 properties)

| Ordinal | Property | Wire kind | Direction | Enum values |
| --- | --- | --- | --- | --- |
| 0 | `rxAntennas` | `utf8` | bidirectional |  |
| 1 | `rxOnlyAntennas` | `utf8` | bidirectional |  |
| 2 | `useTxAntennaForRx` | `bool` | bidirectional |  |
| 3 | `txAntennas` | `utf8` | outbound |  |
| 4 | `blockTxAnt2` | `bool` | outbound |  |
| 5 | `blockTxAnt3` | `bool` | outbound |  |
| 6 | `rxOutOnTx` | `bool` | outbound |  |
| 7 | `ext1OutOnTx` | `bool` | outbound |  |
| 8 | `ext2OutOnTx` | `bool` | outbound |  |
| 9 | `rxOutOverride` | `bool` | outbound |  |

**AmplifierModel** (20 properties)

| Ordinal | Property | Wire kind | Direction | Enum values |
| --- | --- | --- | --- | --- |
| 0 | `connectionPhase` | `enum` | outbound | 0, 1, 2, 3, 4, 5, 6, 7 |
| 1 | `configuredHost` | `utf8` | outbound |  |
| 2 | `configuredPort` | `i64` | outbound |  |
| 3 | `connectionError` | `utf8` | outbound |  |
| 4 | `deviceModel` | `utf8` | outbound |  |
| 5 | `deviceSerial` | `utf8` | outbound |  |
| 6 | `deviceVersion` | `utf8` | outbound |  |
| 7 | `deviceNickname` | `utf8` | outbound |  |
| 8 | `present` | `bool` | outbound |  |
| 9 | `state` | `enum` | outbound | 0, 1, 2, 3, 4, 5, 6, 7 |
| 10 | `deviceState` | `utf8` | outbound |  |
| 11 | `operate` | `bool` | outbound |  |
| 12 | `transmitting` | `bool` | outbound |  |
| 13 | `forwardPowerW` | `f64` | outbound |  |
| 14 | `swr` | `f64` | outbound |  |
| 15 | `temperatureC` | `f64` | outbound |  |
| 16 | `mainsVoltageV` | `f64` | outbound |  |
| 17 | `drainCurrentA` | `f64` | outbound |  |
| 18 | `efficiencyText` | `utf8` | outbound |  |
| 19 | `bandFollow` | `enum` | outbound | 0, 1, 2, 3 |

**DspAssetService** (8 properties)

| Ordinal | Property | Wire kind | Direction | Enum values |
| --- | --- | --- | --- | --- |
| 0 | `nnrStandardAsset` | `utf8` | outbound |  |
| 1 | `nnrPremiumAsset` | `utf8` | outbound |  |
| 2 | `nnrModelSelectionPending` | `bool` | outbound |  |
| 3 | `nnrModelStatus` | `utf8` | outbound |  |
| 4 | `selectionRevision` | `i64` | outbound |  |
| 5 | `nr3ModelAsset` | `utf8` | outbound |  |
| 6 | `nr3ModelStatus` | `utf8` | outbound |  |
| 7 | `nr3Runnable` | `bool` | outbound |  |

**IoBoardHl2Facade** (3 properties)

| Ordinal | Property | Wire kind | Direction | Enum values |
| --- | --- | --- | --- | --- |
| 0 | `detected` | `bool` | outbound |  |
| 1 | `hardwareVersion` | `i64` | outbound |  |
| 2 | `registers` | `utf8` | outbound |  |

**NotchModel** (4 properties)

| Ordinal | Property | Wire kind | Direction | Enum values |
| --- | --- | --- | --- | --- |
| 0 | `listJson` | `utf8` | outbound |  |
| 1 | `revision` | `i64` | outbound |  |
| 2 | `globalEnabled` | `bool` | bidirectional |  |
| 3 | `autoIncrease` | `bool` | bidirectional |  |

**PanadapterModel** (4 properties)

| Ordinal | Property | Wire kind | Direction | Enum values |
| --- | --- | --- | --- | --- |
| 0 | `centerFrequency` | `f64` | bidirectional |  |
| 1 | `bandwidth` | `f64` | bidirectional |  |
| 2 | `dBmFloor` | `i64` | bidirectional |  |
| 3 | `dBmCeiling` | `i64` | bidirectional |  |

**PureSignalSessionFacade** (6 properties)

| Ordinal | Property | Wire kind | Direction | Enum values |
| --- | --- | --- | --- | --- |
| 0 | `available` | `bool` | outbound |  |
| 1 | `canActuate` | `bool` | outbound |  |
| 2 | `twoToneOn` | `bool` | outbound |  |
| 3 | `statusJson` | `utf8` | outbound |  |
| 4 | `lastActionError` | `utf8` | outbound |  |
| 5 | `displayGeneration` | `i64` | outbound |  |

**PureSignalSettings** (10 properties)

| Ordinal | Property | Wire kind | Direction | Enum values |
| --- | --- | --- | --- | --- |
| 0 | `autoCalEnabled` | `bool` | bidirectional |  |
| 1 | `runCalibrationProcessing` | `bool` | bidirectional |  |
| 2 | `autoAttenuate` | `bool` | bidirectional |  |
| 3 | `quickAttenuate` | `bool` | bidirectional |  |
| 4 | `moxDelaySeconds` | `f64` | bidirectional |  |
| 5 | `loopDelaySeconds` | `f64` | bidirectional |  |
| 6 | `requestedTxDelayNs` | `f64` | bidirectional |  |
| 7 | `hardwarePeakOverrideEnabled` | `bool` | bidirectional |  |
| 8 | `hardwarePeakOverride` | `f64` | bidirectional |  |
| 9 | `lastLoadError` | `utf8` | outbound |  |

**RadioModel** (19 properties)

| Ordinal | Property | Wire kind | Direction | Enum values |
| --- | --- | --- | --- | --- |
| 0 | `settingsSaveError` | `utf8` | outbound |  |
| 1 | `receiveLayoutRestoreState` | `utf8` | outbound |  |
| 2 | `receiveLayoutRestoreMessage` | `utf8` | outbound |  |
| 3 | `name` | `utf8` | outbound |  |
| 4 | `model` | `utf8` | outbound |  |
| 5 | `version` | `utf8` | outbound |  |
| 6 | `connected` | `bool` | outbound |  |
| 7 | `rfKitEnabled` | `bool` | outbound |  |
| 8 | `fourO3AEnabled` | `bool` | outbound |  |
| 9 | `fourO3AListening` | `bool` | outbound |  |
| 10 | `fourO3AListenerError` | `utf8` | outbound |  |
| 11 | `rxFilter0Mode` | `i64` | outbound |  |
| 12 | `rxFilter0Effective` | `i64` | outbound |  |
| 13 | `rxFilter0Band` | `i64` | outbound |  |
| 14 | `rxFilter0Reason` | `utf8` | outbound |  |
| 15 | `rxFilter1Mode` | `i64` | outbound |  |
| 16 | `rxFilter1Effective` | `i64` | outbound |  |
| 17 | `rxFilter1Band` | `i64` | outbound |  |
| 18 | `rxFilter1Reason` | `utf8` | outbound |  |

**RfKitModel** (30 properties)

| Ordinal | Property | Wire kind | Direction | Enum values |
| --- | --- | --- | --- | --- |
| 0 | `connectionPhase` | `enum` | outbound | 0, 1, 2, 3, 4, 5, 6, 7 |
| 1 | `configuredHost` | `utf8` | outbound |  |
| 2 | `configuredPort` | `i64` | outbound |  |
| 3 | `connectionError` | `utf8` | outbound |  |
| 4 | `deviceModel` | `utf8` | outbound |  |
| 5 | `deviceSerial` | `utf8` | outbound |  |
| 6 | `deviceVersion` | `utf8` | outbound |  |
| 7 | `deviceNickname` | `utf8` | outbound |  |
| 8 | `present` | `bool` | outbound |  |
| 9 | `operate` | `bool` | outbound |  |
| 10 | `forwardPowerW` | `f64` | outbound |  |
| 11 | `reflectedPowerW` | `f64` | outbound |  |
| 12 | `swr` | `f64` | outbound |  |
| 13 | `temperatureC` | `f64` | outbound |  |
| 14 | `voltageV` | `f64` | outbound |  |
| 15 | `currentA` | `f64` | outbound |  |
| 16 | `operationalInterface` | `utf8` | outbound |  |
| 17 | `antennaPresentMask` | `i64` | outbound |  |
| 18 | `antennaDisabledMask` | `i64` | outbound |  |
| 19 | `activeAntennaNumber` | `i64` | outbound |  |
| 20 | `activeAntennaExternal` | `bool` | outbound |  |
| 21 | `tunerMode` | `enum` | outbound | 0, 1, 2, 3, 4 |
| 22 | `tunerSetup` | `utf8` | outbound |  |
| 23 | `tunerInductanceNh` | `i64` | outbound |  |
| 24 | `tunerCapacitancePf` | `i64` | outbound |  |
| 25 | `tunerFrequencyKhz` | `i64` | outbound |  |
| 26 | `tunerSegmentKhz` | `i64` | outbound |  |
| 27 | `bandFollow` | `enum` | outbound | 0, 1, 2, 3 |
| 28 | `bandFollowAddress` | `utf8` | outbound |  |
| 29 | `bandFollowPort` | `i64` | outbound |  |

**SliceModel** (144 properties)

| Ordinal | Property | Wire kind | Direction | Enum values |
| --- | --- | --- | --- | --- |
| 0 | `outputRoute` | `enum` | bidirectional | 0, 1 |
| 1 | `frequency` | `f64` | bidirectional |  |
| 2 | `dspMode` | `enum` | bidirectional | 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13 |
| 3 | `filterLow` | `i64` | bidirectional |  |
| 4 | `filterHigh` | `i64` | bidirectional |  |
| 5 | `agcMode` | `enum` | bidirectional | 0, 1, 2, 3, 4, 5 |
| 6 | `stepHz` | `i64` | bidirectional |  |
| 7 | `afGain` | `i64` | bidirectional |  |
| 8 | `rfGain` | `i64` | bidirectional |  |
| 9 | `rxAntenna` | `utf8` | bidirectional |  |
| 10 | `txAntenna` | `utf8` | bidirectional |  |
| 11 | `active` | `bool` | outbound |  |
| 12 | `txSlice` | `bool` | outbound |  |
| 13 | `sliceIndex` | `i64` | constantSnapshot |  |
| 14 | `band` | `enum` | outbound | 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26 |
| 15 | `signalStrengthDbm` | `f64` | outbound |  |
| 16 | `signalPeakDbm` | `f64` | outbound |  |
| 17 | `signalAverageDbm` | `f64` | outbound |  |
| 18 | `stationAutoAgcNoiseFloorDbm` | `f64` | outbound |  |
| 19 | `stationAutoAgcNoiseFloorValid` | `bool` | outbound |  |
| 20 | `stationAutoAgcNoiseFloorGeneration` | `i64` | outbound |  |
| 21 | `chainIndex` | `i64` | outbound |  |
| 22 | `ddcIndex` | `i64` | outbound |  |
| 23 | `streamIndex` | `i64` | outbound |  |
| 24 | `streamCtunPinned` | `bool` | outbound |  |
| 25 | `streamEpoch` | `i64` | outbound |  |
| 26 | `shiftOffsetHz` | `f64` | outbound |  |
| 27 | `panKey` | `utf8` | bidirectional |  |
| 28 | `sampleRateHz` | `i64` | outbound |  |
| 29 | `diversityEnabled` | `bool` | bidirectional |  |
| 30 | `diversityPhaseDeg` | `f64` | bidirectional |  |
| 31 | `diversityGainDb` | `f64` | bidirectional |  |
| 32 | `diversityFineNullEnabled` | `bool` | bidirectional |  |
| 33 | `widebandExtensionRequested` | `bool` | outbound |  |
| 34 | `psPaused` | `bool` | outbound |  |
| 35 | `locked` | `bool` | bidirectional |  |
| 36 | `muted` | `bool` | bidirectional |  |
| 37 | `audioPan` | `f64` | bidirectional |  |
| 38 | `ssqlEnabled` | `bool` | bidirectional |  |
| 39 | `ssqlThresh` | `f64` | bidirectional |  |
| 40 | `amsqEnabled` | `bool` | bidirectional |  |
| 41 | `amsqThresh` | `f64` | bidirectional |  |
| 42 | `fmsqEnabled` | `bool` | bidirectional |  |
| 43 | `fmsqThresh` | `f64` | bidirectional |  |
| 44 | `agcThreshold` | `i64` | bidirectional |  |
| 45 | `agcHang` | `i64` | bidirectional |  |
| 46 | `agcSlope` | `i64` | bidirectional |  |
| 47 | `agcAttack` | `i64` | bidirectional |  |
| 48 | `agcDecay` | `i64` | bidirectional |  |
| 49 | `autoAgcEnabled` | `bool` | bidirectional |  |
| 50 | `autoAgcOffset` | `f64` | bidirectional |  |
| 51 | `agcFixedGain` | `i64` | bidirectional |  |
| 52 | `agcHangThreshold` | `i64` | bidirectional |  |
| 53 | `agcMaxGain` | `i64` | bidirectional |  |
| 54 | `ritEnabled` | `bool` | bidirectional |  |
| 55 | `ritHz` | `i64` | bidirectional |  |
| 56 | `xitEnabled` | `bool` | bidirectional |  |
| 57 | `xitHz` | `i64` | bidirectional |  |
| 58 | `nbMode` | `enum` | bidirectional | 0, 1, 2 |
| 59 | `activeNr` | `enum` | bidirectional | 0, 1, 2, 3, 4, 5, 6, 7, 8 |
| 60 | `nnrModelSlot` | `i64` | bidirectional |  |
| 61 | `nnrMaskFloorDb` | `f64` | bidirectional |  |
| 62 | `nnrPosition` | `enum` | bidirectional | 0, 1 |
| 63 | `nnrAlpha` | `f64` | bidirectional |  |
| 64 | `nnrAlphaKneeDb` | `f64` | bidirectional |  |
| 65 | `nnrTauSeconds` | `f64` | bidirectional |  |
| 66 | `nnrMaxGainDb` | `f64` | bidirectional |  |
| 67 | `nnrAttackMs` | `f64` | bidirectional |  |
| 68 | `nnrReleaseMs` | `f64` | bidirectional |  |
| 69 | `nnrAvailable` | `bool` | outbound |  |
| 70 | `nnrReady` | `bool` | outbound |  |
| 71 | `nnrRunning` | `bool` | outbound |  |
| 72 | `nnrStandardAvailable` | `bool` | outbound |  |
| 73 | `nnrPremiumAvailable` | `bool` | outbound |  |
| 74 | `nnrRateSupported` | `bool` | outbound |  |
| 75 | `nnrActualModelSlot` | `i64` | outbound |  |
| 76 | `nnrDspRateHz` | `i64` | outbound |  |
| 77 | `nnrNetworkRateHz` | `i64` | outbound |  |
| 78 | `nnrDelaySamples` | `i64` | outbound |  |
| 79 | `nnrProfilingAvailable` | `bool` | outbound |  |
| 80 | `nnrLatencyMs` | `f64` | outbound |  |
| 81 | `nnrTestMode` | `i64` | outbound |  |
| 82 | `nnrOutputMode` | `i64` | outbound |  |
| 83 | `nnrModelSource` | `utf8` | outbound |  |
| 84 | `nnrStatus` | `utf8` | outbound |  |
| 85 | `nnrLastError` | `utf8` | outbound |  |
| 86 | `nnrLimit` | `i64` | outbound |  |
| 87 | `nr1Taps` | `i64` | bidirectional |  |
| 88 | `nr1Delay` | `i64` | bidirectional |  |
| 89 | `nr1Gain` | `f64` | bidirectional |  |
| 90 | `nr1Leakage` | `f64` | bidirectional |  |
| 91 | `nr1Position` | `enum` | bidirectional | 0, 1 |
| 92 | `nr2GainMethod` | `enum` | bidirectional | 0, 1, 2, 3 |
| 93 | `nr2NpeMethod` | `enum` | bidirectional | 0, 1, 2 |
| 94 | `nr2TrainT1` | `f64` | bidirectional |  |
| 95 | `nr2TrainT2` | `f64` | bidirectional |  |
| 96 | `nr2AeFilter` | `bool` | bidirectional |  |
| 97 | `nr2Position` | `enum` | bidirectional | 0, 1 |
| 98 | `nr2Post2Run` | `bool` | bidirectional |  |
| 99 | `nr2Post2Level` | `f64` | bidirectional |  |
| 100 | `nr2Post2Factor` | `f64` | bidirectional |  |
| 101 | `nr2Post2Rate` | `f64` | bidirectional |  |
| 102 | `nr2Post2Taper` | `i64` | bidirectional |  |
| 103 | `nr3Position` | `enum` | bidirectional | 0, 1 |
| 104 | `nr3UseDefaultGain` | `bool` | bidirectional |  |
| 105 | `nr4Reduction` | `f64` | bidirectional |  |
| 106 | `nr4Smoothing` | `f64` | bidirectional |  |
| 107 | `nr4Whitening` | `f64` | bidirectional |  |
| 108 | `nr4Rescale` | `f64` | bidirectional |  |
| 109 | `nr4PostThresh` | `f64` | bidirectional |  |
| 110 | `nr4Algo` | `enum` | bidirectional | 0, 1, 2 |
| 111 | `dfnrAttenLimit` | `f64` | bidirectional |  |
| 112 | `dfnrPostFilterBeta` | `f64` | bidirectional |  |
| 113 | `bnrStrength` | `f64` | bidirectional |  |
| 114 | `mnrStrength` | `f64` | bidirectional |  |
| 115 | `mnrOversub` | `f64` | bidirectional |  |
| 116 | `mnrFloor` | `f64` | bidirectional |  |
| 117 | `mnrAlpha` | `f64` | bidirectional |  |
| 118 | `mnrBias` | `f64` | bidirectional |  |
| 119 | `mnrGsmooth` | `f64` | bidirectional |  |
| 120 | `snbEnabled` | `bool` | bidirectional |  |
| 121 | `anfEnabled` | `bool` | bidirectional |  |
| 122 | `nb1Threshold` | `i64` | bidirectional |  |
| 123 | `nb1TransitionMs` | `f64` | bidirectional |  |
| 124 | `nb1LeadMs` | `f64` | bidirectional |  |
| 125 | `nb1LagMs` | `f64` | bidirectional |  |
| 126 | `nb2Mode` | `i64` | bidirectional |  |
| 127 | `snbK1` | `f64` | bidirectional |  |
| 128 | `snbK2` | `f64` | bidirectional |  |
| 129 | `snbOutputBandwidthHz` | `i64` | bidirectional |  |
| 130 | `apfEnabled` | `bool` | bidirectional |  |
| 131 | `apfTuneHz` | `i64` | bidirectional |  |
| 132 | `binauralEnabled` | `bool` | bidirectional |  |
| 133 | `fmCtcssMode` | `i64` | bidirectional |  |
| 134 | `fmCtcssValueHz` | `f64` | bidirectional |  |
| 135 | `fmOffsetHz` | `i64` | bidirectional |  |
| 136 | `fmTxMode` | `enum` | bidirectional | 0, 1, 2 |
| 137 | `fmReverse` | `bool` | bidirectional |  |
| 138 | `diglOffsetHz` | `i64` | bidirectional |  |
| 139 | `diguOffsetHz` | `i64` | bidirectional |  |
| 140 | `rttyMarkHz` | `i64` | bidirectional |  |
| 141 | `rttyShiftHz` | `i64` | bidirectional |  |
| 142 | `snrDb` | `f64` | outbound |  |
| 143 | `lastRadeRxCallsign` | `utf8` | outbound |  |

**StationTciModel** (5 properties)

| Ordinal | Property | Wire kind | Direction | Enum values |
| --- | --- | --- | --- | --- |
| 0 | `enabled` | `bool` | outbound |  |
| 1 | `port` | `i64` | outbound |  |
| 2 | `listening` | `bool` | outbound |  |
| 3 | `stationAddress` | `utf8` | outbound |  |
| 4 | `error` | `utf8` | outbound |  |

**StepAttenuatorFacade** (15 properties)

| Ordinal | Property | Wire kind | Direction | Enum values |
| --- | --- | --- | --- | --- |
| 0 | `enabled` | `bool` | bidirectional |  |
| 1 | `attenuationDb` | `i64` | bidirectional |  |
| 2 | `preampMode` | `i64` | bidirectional |  |
| 3 | `rx1Preamp` | `bool` | bidirectional |  |
| 4 | `autoAttEnabled` | `bool` | bidirectional |  |
| 5 | `autoAttMode` | `i64` | bidirectional |  |
| 6 | `autoAttUndo` | `bool` | bidirectional |  |
| 7 | `autoAttUndoDelayMs` | `i64` | bidirectional |  |
| 8 | `autoAttHoldMs` | `i64` | bidirectional |  |
| 9 | `minDb` | `i64` | outbound |  |
| 10 | `maxDb` | `i64` | outbound |  |
| 11 | `autoAttApplied` | `bool` | outbound |  |
| 12 | `overloadAdc0` | `i64` | outbound |  |
| 13 | `overloadAdc1` | `i64` | outbound |  |
| 14 | `adcLinked` | `bool` | outbound |  |

**TransmitModel** (15 properties)

| Ordinal | Property | Wire kind | Direction | Enum values |
| --- | --- | --- | --- | --- |
| 0 | `mox` | `bool` | bidirectional |  |
| 1 | `tune` | `bool` | bidirectional |  |
| 2 | `power` | `i64` | bidirectional |  |
| 3 | `micGain` | `f64` | bidirectional |  |
| 4 | `pureSig` | `bool` | bidirectional |  |
| 5 | `filterLow` | `i64` | bidirectional |  |
| 6 | `filterHigh` | `i64` | bidirectional |  |
| 7 | `lineInGain` | `i64` | bidirectional |  |
| 8 | `userDigOut` | `i64` | bidirectional |  |
| 9 | `forceAttwhenPSAoff` | `bool` | bidirectional |  |
| 10 | `forceAttwhenPowerChangesWhenPSAon` | `bool` | bidirectional |  |
| 11 | `forceAttwhenPowerChangesWhenPSAonAndDecreased` | `bool` | bidirectional |  |
| 12 | `antiVoxTauMs` | `i64` | bidirectional |  |
| 13 | `antiVoxRun` | `bool` | bidirectional |  |
| 14 | `paSettingsBypass` | `bool` | bidirectional |  |

**TunerModel** (21 properties)

| Ordinal | Property | Wire kind | Direction | Enum values |
| --- | --- | --- | --- | --- |
| 0 | `connectionPhase` | `enum` | outbound | 0, 1, 2, 3, 4, 5, 6, 7 |
| 1 | `configuredHost` | `utf8` | outbound |  |
| 2 | `configuredPort` | `i64` | outbound |  |
| 3 | `connectionError` | `utf8` | outbound |  |
| 4 | `deviceModel` | `utf8` | outbound |  |
| 5 | `deviceSerial` | `utf8` | outbound |  |
| 6 | `deviceVersion` | `utf8` | outbound |  |
| 7 | `deviceNickname` | `utf8` | outbound |  |
| 8 | `relayC1` | `i64` | outbound |  |
| 9 | `relayL` | `i64` | outbound |  |
| 10 | `relayC2` | `i64` | outbound |  |
| 11 | `isOperate` | `bool` | outbound |  |
| 12 | `isBypass` | `bool` | outbound |  |
| 13 | `isTuning` | `bool` | outbound |  |
| 14 | `antennaA` | `i64` | outbound |  |
| 15 | `hasAntennaSwitch` | `bool` | outbound |  |
| 16 | `isPresent` | `bool` | outbound |  |
| 17 | `hasDirectConnection` | `bool` | outbound |  |
| 18 | `tgxlIp` | `utf8` | outbound |  |
| 19 | `fwdPower` | `f64` | outbound |  |
| 20 | `swr` | `f64` | outbound |  |

<!-- /surface -->

Objects are addressed by key. Slices are `slice:<id>` and are created and
destroyed during the session.

<!-- surface:objectKeys -->
<!-- Generated by scripts/render-link-tables.py from tests/data/link/v1/surface.json. Do not edit by hand. -->

| Object key | Class |
| --- | --- |
| `radio` | `RadioModel` |
| `pureSignalSettings` | `PureSignalSettings` |
| `dspAssets` | `DspAssetService` |
| `notches` | `NotchModel` |
| `stepAtt` | `StepAttenuatorFacade` |
| `alexAntennas` | `AlexAntennaFacade` |
| `ioBoard` | `IoBoardHl2Facade` |
| `pureSignal` | `PureSignalSessionFacade` |
| `transmit` | `TransmitModel` |
| `tuner` | `TunerModel` |
| `amplifier` | `AmplifierModel` |
| `rfkit` | `RfKitModel` |
| `stationTci` | `StationTciModel` |
| `pan:<i>` | `PanadapterModel` |
| `slice:<id>` | `SliceModel` |

<!-- /surface -->

Notes on the keys:

- **`pan:<i>`.** The station watches every panadapter its model holds, but
  no code path in `nereusd` adds one: panadapters live in the window, and
  the Core's spectrum travels on display endpoints (media control). A real
  Core therefore sends no `pan:<i>` objects today. The surface records one
  so the key pattern is known.
- **Minor 11 objects.** `stepAtt`, `alexAntennas` and `ioBoard` are sent
  only to a peer at agreed minor 11 and only while `radioHardwareVersion` is
  at least 1, 2 and 3 respectively. `amplifier` and `rfkit` are sent only at
  minor 11 on a Core that owns its accessories, and `stationTci` only at
  minor 11 on a Core that runs a station TCI server
  (`StationServer::sendToSession`). An older peer never sees their schema
  either.
- **Unknown classes.** A client that receives a schema for a class it does
  not know records the difference and drops that class's objects and
  deltas.

### 7.2 Deltas

The station collects property changes and sends them at most every 50 ms
(`kDefaultDeltaFlushMs`), one `delta` per object carrying the latest value
of each changed property. The desktop client collects its own writes on
the same 50 ms period (`kDefaultWriteFlushMs`).

### 7.3 Property writes and property.result

A client changes a `bidirectional` property with `property.write`: the
object `key`, a list of property entries and, from a client that
negotiated results, a nonzero `writeId`.

The station applies each entry and refuses, with a reason, a duplicate
property in one write, an unknown property or a wrong wire kind, an
`outbound` property, a read-only object (`ioBoard`, `amplifier`, `rfkit`,
`stationTci`), a `stepAtt` or `alexAntennas` write from a peer below minor
11 or while the Core has no controller behind it, a raw write of `radio`'s
`rfKitEnabled` (changed only with `setRfKitEnabled`), transmit
configuration on a receive-only station, and DSP settings from a peer that
did not negotiate them (`StationServer::handlePropertyWrite`).
A write to an `outbound` property is refused before anything is applied,
with the reason "The station sets this itself; it cannot be changed from
here.", unless one of the earlier, more specific refusals above applies
first; this covers the station's own readings, such as a slice's signal
strength, as well as properties changed through a command.

After the whole batch, the station reads each property back. When the
agreed minor is at least 5 (`kDspControlSessionProtocolMinor`) and the
write carried a `writeId`, it answers with `property.result`: the same
`key` and `writeId` and one result per property, `{property, accepted,
reason, hasValue, value}`, where `value` is the value the station kept. A
write the station kept at a different value is not accepted, and its reason
says why. The desktop client reads results only when `propertyResultVersion`
is at least 1 (`propertyResultsAvailable()`).

Side effects of a write on other properties go back as a `delta`. A peer
that did not negotiate results, or wrote without a `writeId`, also gets its
requested properties back in that `delta`.

A property write never keys the transmitter: `txPermitted` is false and
the transmit safety gates stay at the station (section 17).

## 8. The settings proxy

Settings are a flat space of string keys and string values, separate from
the object mirror. Each key is either station-scoped (kept by the Core,
proxied to the client) or operator-local (kept by the client's own
computer, never sent). `classifySettingsKey` (`SettingsScope.cpp`) decides:

1. Keys the Core owns by code (below) are station-scoped, before any table.
2. A trailing `_<digits>` (a panadapter index) is ignored when matching.
3. Exact exception keys, then prefixes, then whole keys; the first match
   wins. A key that matches nothing is operator-local. No station prefix
   and no operator-local prefix start one another, so the table's order
   between the two scopes does not change a result.

<!-- surface:settingsScope -->
<!-- Generated by scripts/render-link-tables.py from tests/data/link/v1/surface.json. Do not edit by hand. -->

| Tier | Key or prefix | Scope |
| --- | --- | --- |
| 1. exact key (exception) | `TciLogWindowGeometry` | operatorLocal |
| 1. exact key (exception) | `TciLogWindowAutoScroll` | operatorLocal |
| 1. exact key (exception) | `FreeDvReporter/ColumnWidths` | operatorLocal |
| 1. exact key (exception) | `FreeDvReporter/SortColumn` | operatorLocal |
| 1. exact key (exception) | `FreeDvReporter/SortAscending` | operatorLocal |
| 1. exact key (exception) | `FreeDvReporter/VisibleColumns` | operatorLocal |
| 1. exact key (exception) | `FreeDvReporter/ColumnFilters` | operatorLocal |
| 1. exact key (exception) | `FreeDvReporter/BandFilter` | operatorLocal |
| 1. exact key (exception) | `FreeDvReporter/Hidden` | operatorLocal |
| 1. exact key (exception) | `FreeDvReporter/DistanceMiles` | operatorLocal |
| 1. exact key (exception) | `FreeDvReporter/DirectionAsCardinal` | operatorLocal |
| 1. exact key (exception) | `FreeDvReporter/FrequencyAsKhz` | operatorLocal |
| 2. prefix | `hardware/` | station |
| 2. prefix | `Slice` | station |
| 2. prefix | `Vfo` | station |
| 2. prefix | `PGXL_` | station |
| 2. prefix | `TGXL_` | station |
| 2. prefix | `RfKit_` | station |
| 2. prefix | `StationTci_` | station |
| 2. prefix | `DxCluster` | station |
| 2. prefix | `Rbn` | station |
| 2. prefix | `Pota` | station |
| 2. prefix | `PskReporter` | station |
| 2. prefix | `FreeDv` | station |
| 2. prefix | `SpotCollector` | station |
| 2. prefix | `Wsjtx` | station |
| 2. prefix | `User/` | station |
| 2. prefix | `Notch` | station |
| 2. prefix | `DspOptions` | station |
| 2. prefix | `Nb` | station |
| 2. prefix | `Snb` | station |
| 2. prefix | `Rade` | station |
| 2. prefix | `Tci` | operatorLocal |
| 2. prefix | `radios/` | operatorLocal |
| 2. prefix | `ConnectionTargets/` | operatorLocal |
| 2. prefix | `RemoteVax/` | operatorLocal |
| 3. whole key | `DisplayFftSize` | station |
| 3. whole key | `DisplayFftWindow` | station |
| 3. whole key | `DisplayHzPerBinTarget` | station |
| 3. whole key | `DisplaySpectrumFps` | station |
| 3. whole key | `DisplayTxFftSize` | station |
| 3. whole key | `DisplayTxWindowType` | station |
| 3. whole key | `DisplayTxPanDetector` | station |
| 3. whole key | `DisplayTxPanAveraging` | station |
| 3. whole key | `DisplayTxPanAvTimeMs` | station |
| 3. whole key | `DisplayTxPanNormalize` | station |
| 3. whole key | `DisplayTxWfDetector` | station |
| 3. whole key | `DisplayTxWfAveraging` | station |
| 3. whole key | `DisplayTxWfAvTimeMs` | station |
| 3. whole key | `audio/DspRate` | station |
| 3. whole key | `audio/DspBlockSize` | station |
| 3. whole key | `BandPlanName` | station |
| 3. whole key | `BandPlanRegion` | station |
| 3. whole key | `Region` | station |
| 3. whole key | `CWPitch` | station |
| 3. whole key | `Nr3ModelPath` | station |
| 3. whole key | `StationCallsign` | station |
| 3. whole key | `RX1_MeterCalOffsetDb` | station |
| 3. whole key | `PeripheralsMigrationDone` | station |
| 3. whole key | `SwrProtectionEnabled` | station |
| 3. whole key | `SwrProtectionLimit` | station |
| 3. whole key | `SwrTuneProtectionEnabled` | station |
| 3. whole key | `TunePowerSwrIgnore` | station |
| 3. whole key | `TxInhibitMonitorEnabled` | station |
| 3. whole key | `TxInhibitMonitorReversed` | station |
| 3. whole key | `WindBackPowerSwr` | station |
| 3. whole key | `MultimeterDelayMs` | station |
| 3. whole key | `DisableHfPa` | operatorLocal |
| 3. whole key | `ExtendedTxAllowed` | operatorLocal |
| 3. whole key | `PreventTxOnDifferentBandToRx` | operatorLocal |
| 3. whole key | `NetworkWatchdogEnabled` | operatorLocal |
| 3. whole key | `RxOnly` | operatorLocal |

<!-- /surface -->

### 8.1 Snapshot and write-through

- `settings.snapshot` carries every station-scoped key the Core holds for
  the connected radio: the `hardware/<mac>/` keys of the connected radio,
  `hardware/oc/`, every other station-scoped key, and the Core's
  profile-seeded marker when present (`SettingsProxyServer::buildSnapshot`). Values are `utf8` entries named
  by their key. Booleans are the strings `"True"` and `"False"`.
- `settings.write` carries one key, its value and an `origin` tag. The
  origin tag is the writing client's session identifier, not a sequence
  number. The station writes the value to its own store and sends
  `settings.value` for the key with the same `origin`, so the writer can
  recognise its own echo. A change the Core makes itself goes out as
  `settings.value` with an empty origin. A removal goes out as
  `settings.value` with no property entry.
- `settings.remove` removes a station-scoped key; the station ignores (and
  logs) a remove of an operator-local key.
- A refused write or remove gets `settings.reject`: the key, the station's
  own value as the property entry when it has one, and a `reason`. The
  client puts that value back.

The station refuses a write to a key outside the station scope ("key is not
Station-scoped"), to another radio's `hardware/<mac>/` keys ("These settings
are for a radio this Core is not connected to."), an out-of-range
`SwrProtectionLimit` (1.0 to 5.0), and transmit-side keys on a receive-only
station.

### 8.2 Keys the Core owns by code

These families are not in the tables above: code, not a table, decides
them (`isModelOwnedDspSettingsKey` and its helpers, `SettingsScope.cpp`).
They change only through their objects and commands; a raw
`settings.write` or `settings.remove` of one is refused with
`settings.reject` and the station's own value.

| Family | Changed through | Refusal reason |
| --- | --- | --- |
| `dspAssets/...` | `dspAssets.*` commands | generic (below) |
| `NotchCount`, `Notch<N>Center`, `Notch<N>Width`, `Notch<N>Active`, `NotchGlobalEnabled`, `NotchAutoIncrease` | `notches` object, `notch.*` commands | "This Core keeps its own notch list. Update this app to change notches." |
| `hardware/<mac>/options/stepAtt/...`, `.../autoAtt/...`, `.../preamp/...` | `stepAtt` object | "This Core keeps its own attenuator and preamp settings. Update this app to change them." |
| `hardware/<mac>/alex/antenna/...` | `alexAntennas` object, `setAlexRxAntenna` | "This Core keeps its own antenna settings. Update this app to change them." |
| `Nr3ModelPath` | `dspAssets.selectNr3Model` | "This Core keeps its own NR3 models. Update this app to choose one." |
| `hardware/<mac>/puresignal/...` | `pureSignalSettings` object, `ps3.*` commands | generic (below) |
| `hardware/<mac>/slices/<n>/nnr/...` | slice properties, `nnr.*` commands | generic (below) |

Matching is case-insensitive. The reasons in the table are plain operator
wording. The generic families keep an older wire reason: "Use the station
DSP controls; raw settings writes cannot bypass model validation." for a
write, and "Use the validated DSP controls to change these settings." for a
remove.

## 9. Commands

### 9.1 Invoke and result

A client asks the station to act with `command.invoke`: a `verb`, an `id`
and `args`, a list of property entries. The station answers each with
`command.result`: the same `verb` and `id`, `accepted`, `reason` (empty on
success), `affected` (the object keys the command changed) and, for
commands that return data, `values`, a list of property entries. For the
`nnr.*`, `ps3.*` and `dspAssets.*` families the `id` must be a whole number
from 1 to 4294967295 or the message is refused.

A PureSignal action can answer more than once: each result carries a
`phase` value of `accepted`, `pending`, `completed` or `failed`, and the
last is `completed` or `failed`.

Every handler requires exactly the arguments listed; none is optional
today, and an extra or missing argument is refused.

<!-- surface:commands -->
<!-- Generated by scripts/render-link-tables.py from tests/data/link/v1/surface.json. Do not edit by hand. -->

| Verb | Arguments | Capability | Capability version | Minimum minor |
| --- | --- | --- | --- | --- |
| `addSlice` | `initialPanId` utf8 | none | 0 | 0 |
| `removeSlice` | `sliceId` i64 | none | 0 | 0 |
| `requestSliceSampleRate` | `sliceId` i64, `rateHz` i64 | none | 0 | 0 |
| `addSliceOnPan` | `panId` utf8 | none | 0 | 0 |
| `setActiveSliceById` | `sliceId` i64 | none | 0 | 0 |
| `requestStreamCtunPinned` | `sliceId` i64, `pinned` bool | `remoteCtunVersion` | 1 | 2 |
| `requestStreamCentre` | `sliceId` i64, `centreHz` f64 | `remoteCtunVersion` | 1 | 2 |
| `configureTgxl` | `host` utf8, `port` i64 | `remoteTgxlConfigVersion` | 1 | 4 |
| `disconnectTgxl` | none | `remoteTgxlConfigVersion` | 1 | 4 |
| `setFourO3AEnabled` | `enabled` bool | `remoteFourO3AControlVersion` | 1 | 4 |
| `configurePgxl` | `host` utf8, `port` i64 | `remotePgxlControlVersion` | 2 | 11 |
| `disconnectPgxl` | none | `remotePgxlControlVersion` | 2 | 11 |
| `setPgxlConnectionSettings` | `autoReconnect` bool, `keepaliveSec` i64, `pingSec` i64 | `remotePgxlControlVersion` | 2 | 11 |
| `configureRfKit` | `host` utf8, `port` i64 | `remoteRfKitControlVersion` | 2 | 11 |
| `disconnectRfKit` | none | `remoteRfKitControlVersion` | 2 | 11 |
| `setRfKitEnabled` | `enabled` bool | `remoteRfKitControlVersion` | 2 | 11 |
| `setStationTci` | `enabled` bool, `port` i64 | `stationTciVersion` | 1 | 11 |
| `requestIoBoardProbe` | none | `radioHardwareVersion` | 2 | 11 |
| `setAlexRxAntenna` | `band` i64, `antenna` i64, `rxOnly` bool | `radioHardwareVersion` | 3 | 11 |
| `nnr.setDiagnostics` | `sliceId` i64, `testMode` i64, `outputMode` i64 | `nnrVersion` | 1 | 5 |
| `nnr.resetTuning` | `sliceId` i64 | `nnrVersion` | 1 | 5 |
| `nnr.tryAgain` | `sliceId` i64 | `nnrVersion` | 1 | 11 |
| `nnr.applyModelSelection` | `revision` i64 | `dspAssetVersion` | 1 | 5 |
| `dspAssets.list` | none | `dspAssetVersion` | 1 | 5 |
| `dspAssets.beginImport` | `kind` i64, `label` utf8, `size` i64, `hash` utf8, `radioIdentity` utf8 | `dspAssetVersion` | 1 | 5 |
| `dspAssets.chunk` | `transferId` utf8, `offset` i64, `data` utf8 | `dspAssetVersion` | 1 | 5 |
| `dspAssets.finishImport` | `transferId` utf8 | `dspAssetVersion` | 1 | 5 |
| `dspAssets.cancelImport` | `transferId` utf8 | `dspAssetVersion` | 1 | 5 |
| `dspAssets.export` | `id` utf8, `offset` i64 | `dspAssetVersion` | 1 | 5 |
| `dspAssets.selectNnrModel` | `slot` i64, `id` utf8 | `dspAssetVersion` | 1 | 5 |
| `dspAssets.selectNr3Model` | `id` utf8 | `dspAssetVersion` | 2 | 5 |
| `ps3.subscribeDisplay` | `enabled` bool | `psDisplayVersion` | 1 | 1 |
| `ps3.off` | none | `psAlgorithmVersion` | 3 | 5 |
| `ps3.single` | none | `psAlgorithmVersion` | 3 | 5 |
| `ps3.automatic` | none | `psAlgorithmVersion` | 3 | 5 |
| `ps3.applyCurrent` | none | `psAlgorithmVersion` | 3 | 5 |
| `ps3.twoTone` | `enabled` bool | `psAlgorithmVersion` | 3 | 5 |
| `ps3.saveCorrection` | `label` utf8 | `psAlgorithmVersion` | 3 | 5 |
| `ps3.restoreCorrection` | `assetId` utf8 | `psAlgorithmVersion` | 3 | 5 |
| `notch.add` | `sliceId` i64, `centreHz` f64, `widthHz` f64 | `notchControlVersion` | 1 | 5 |
| `notch.move` | `id` i64, `centreHz` f64, `widthHz` f64 | `notchControlVersion` | 1 | 5 |
| `notch.setActive` | `id` i64, `active` bool | `notchControlVersion` | 1 | 5 |
| `notch.delete` | `id` i64 | `notchControlVersion` | 1 | 5 |

<!-- /surface -->

The table's capability columns are the gate the desktop client applies
before sending (section 6.2).

### 9.2 Unknown verbs

A verb the station does not route gets `command.result` with `accepted`
false and the reason "unrecognised command verb" (or "Unknown PureSignal
action." and "Unknown DSP asset action." inside those families). The
connection stays up.

### 9.3 Verbs from an older peer

The station refuses a gated verb from a peer whose agreed minor is below
the verb's minimum, with a plain reason such as "Update this app to set up
the RF-Kit amplifier on this Core." (`StationServer::onTransportText`).

## 10. Telemetry

When the agreed minor is at least 3 and `stationTelemetryVersion` is at
least 1, the station sends one `station.metrics.v1` message a second
(`DaemonTelemetryController::kSamplePeriodMs` 1000) after
`snapshot.complete`. Its `payload` is an object of observations; telemetry
is never a command. The client drops a message whose `sequence` is not
higher than the last, or whose `sampledElapsedMs` went backwards, and
ignores host fields unless the agreed minor is at least 10 and the version
at least 2, and receiver fields unless the minor is at least 11 and the
version at least 3. A message over 16 KiB is refused.

<!-- surface:telemetry -->
<!-- Generated by scripts/render-link-tables.py from tests/data/link/v1/surface.json. Do not edit by hand. -->

Message kind `station.metrics.v1`.

| `stationTelemetryVersion` | Minimum minor | Field paths | Field paths added |
| --- | --- | --- | --- |
| 1 | 3 | 15 | `audio.active`, `audio.contextGeneration`, `audio.encodeFailuresPerSecond`, `audio.encodedPacketsPerSecond`, `audio.sendAcceptedPerSecond`, `audio.sendRejectedPerSecond`, `audio.sourceDropsPerSecond`, `audio.sourceFramesPerSecond`, `radio.connected`, `radio.rttAgeMs`, `radio.rttMs`, `radio.rxMbps`, `radio.txMbps`, `sampledElapsedMs`, `sequence` |
| 2 | 10 | 22 | `host.hottestZoneCelsius`, `host.hottestZoneName`, `host.memoryAvailableKiB`, `host.memoryTotalKiB`, `host.processCpuPercent`, `host.processResidentKiB`, `host.systemCpuPercent` |
| 3 | 11 | 26 | `receivers[].inputDelayMs`, `receivers[].loadPercent`, `receivers[].skippedInputMs`, `receivers[].sliceId` |

<!-- /surface -->

## 11. Media control

`media.control` carries one operation object in `payload`, with an `op`
key naming it. The operations, their exact keys and their sequencing are
specified in
[remote media control version 1](2026-09-20-remote-media-control-v1.md);
the table lists the keys each operation carries as the code builds and
checks them. A whole `media.control` message over 128 KiB is refused. The
station accepts media control only from the current session, after
`snapshot.complete`, when media is available.

<!-- surface:mediaControl -->
<!-- Generated by scripts/render-link-tables.py from tests/data/link/v1/surface.json. Do not edit by hand. -->

Message kind `media.control`; the operation's keys sit in `payload`.

Client to station:

| Operation (`op`) | Capability | Fields always present | Fields present with | Object-valued fields |
| --- | --- | --- | --- | --- |
| `audio` | `remoteMediaVersion` | `connectionId`, `enabled`, `op`, `revision` | `profile` with audioProfileVersion, remoteAudioStatusVersion | none |
| `candidate` | `remoteMediaVersion` | `candidate`, `connectionId`, `mid`, `op` | none | none |
| `clock-probe` | `audioClockVersion` | `connectionId`, `id`, `op`, `t0` | none | none |
| `description` | `remoteMediaVersion` | `connectionId`, `op`, `sdp`, `type` | none | none |
| `headphones-audio` | `headphonesMixVersion` | `connectionId`, `enabled`, `op`, `profile`, `revision` | none | none |
| `keyframe` | `remoteMediaVersion` | `connectionId`, `contextGeneration`, `endpointId`, `op` | none | none |
| `receiver-audio` | `receiverAudioVersion` | `connectionId`, `enabled`, `op`, `profile`, `revision`, `sliceId` | none | none |
| `start` | `remoteMediaVersion` | `connectionId`, `op` | `audioProfileVersion` with audioProfileVersion; `headphonesMixVersion` with headphonesMixVersion; `receiverAudioVersion` with receiverAudioVersion | none |
| `subscribe` | `remoteMediaVersion` | `centreHz`, `connectionId`, `endpointId`, `fftSize`, `fps`, `framesPerLine`, `maxDbm`, `minDbm`, `op`, `pixels`, `revision`, `sliceId`, `spanHz`, `tier`, `trace`, `waterfall`, `wideSpanFactor`, `windowType` | `extendedView` with remoteWidebandDisplayVersion | `trace`: {averageAlpha, averageMode, detector}; `waterfall`: {averageAlpha, averageMode, detector} |
| `unsubscribe` | `remoteMediaVersion` | `connectionId`, `endpointId`, `op` | `revision` with remoteDisplayBudgetVersion | none |

Station to client:

| Operation (`op`) | Capability | Fields always present | Fields present with | Object-valued fields |
| --- | --- | --- | --- | --- |
| `allocation-result` | `remoteDisplayBudgetVersion` | `accepted`, `acceptedRevision`, `applicationBytesPerSecond`, `budgetGeneration`, `connectionId`, `endpointId`, `messagesPerSecond`, `op`, `reason`, `revision`, `spectrumSampleUnitsPerSecond` | none | none |
| `audio-context` | `remoteMediaVersion` | `connectionId`, `enabled`, `firstSequence`, `firstTimestamp`, `generation`, `op`, `revision`, `ssrc` | `encoder` with enabled=true, remoteAudioStatusVersion; `profile` with audioProfileVersion, remoteAudioStatusVersion; `profileRefusal` with audioProfileVersion, profile=opus, profileRefused, remoteAudioStatusVersion; `reason` with enabled=false, remoteAudioStatusVersion | `encoder`: {audioBandwidthHz, channels, codec, frameSamples, sampleRate, targetBitrate} or {bitsPerSample, channels, codec, frameSamples, payloadType, sampleRate} |
| `candidate` | `remoteMediaVersion` | `candidate`, `connectionId`, `mid`, `op` | none | none |
| `clock-echo` | `audioClockVersion` | `capturedNs`, `connectionId`, `generation`, `id`, `op`, `rtpTimestamp`, `t0`, `t1`, `t2` | none | none |
| `context` | `remoteMediaVersion` | `centreHz`, `connectionId`, `contextGeneration`, `endpointId`, `fps`, `framesPerLine`, `maxDbm`, `minDbm`, `op`, `revision`, `sampleRateHz`, `sourceCentreHz`, `sourceStream`, `spanHz`, `traceSamples`, `waterfallSamples`, `wideCentreHz`, `wideSamples`, `wideSpanHz` | `grantedFftSize` with spectrumGrantVersion; `grantedPixels` with spectrumGrantVersion; `grantedTier` with spectrumGrantVersion; `limit` with spectrumGrantVersion; `requestedPixels` with spectrumGrantVersion; `wideband` with remoteWidebandDisplayVersion | `wideband`: {active, adcRateHz, available, filterChainIndex, geometryRateBasis, highHz, levelReference, lowHz, physicalAdcIndex, sourceGeneration, version} or {active, available, version} |
| `description` | `remoteMediaVersion` | `connectionId`, `op`, `sdp`, `type` | none | none |
| `headphones-audio-context` | `headphonesMixVersion` | `connectionId`, `enabled`, `firstSequence`, `firstTimestamp`, `generation`, `op`, `profile`, `revision`, `ssrc` | `encoder` with enabled=true; `profileRefusal` with profile=opus, profileRefused; `reason` with enabled=false | `encoder`: {audioBandwidthHz, channels, codec, frameSamples, sampleRate, targetBitrate} or {bitsPerSample, channels, codec, frameSamples, payloadType, sampleRate} |
| `noise-floor` | `remoteMediaVersion` | `connectionId`, `contextGeneration`, `endpointId`, `floorDbm`, `op`, `revision` | none | none |
| `receiver-audio-context` | `receiverAudioVersion` | `connectionId`, `enabled`, `firstSequence`, `firstTimestamp`, `generation`, `op`, `profile`, `revision`, `sliceId`, `ssrc` | `encoder` with enabled=true; `profileRefusal` with profile=opus, profileRefused; `reason` with enabled=false | `encoder`: {audioBandwidthHz, channels, codec, frameSamples, sampleRate, targetBitrate} or {bitsPerSample, channels, codec, frameSamples, payloadType, sampleRate} |
| `rejected` | `remoteMediaVersion` | `connectionId`, `endpointId`, `op`, `reason`, `revision` | none | none |

<!-- /surface -->

## 12. Liveness, deadlines, caps and ending

### 12.1 Heartbeat

Each end sends a WebSocket ping every 20 s (`kDefaultHeartbeatIntervalMs`
20000, the same on both ends). A pong clears the count. At a tick where 2
pings (`kDefaultMaxMissedPongs`) are still unanswered, the station declares
the link dead and ends it with "heartbeat timeout", `retryable` true. The
round-trip time of a pong is recorded for diagnostics only; a slow pong is
never a missed one.

### 12.2 The connect deadline

The whole connect sequence must finish within 30 s
(`kStationHandshakeDeadlineMs` 30000) of the WebSocket opening. The
station ends a connection that has not reached `snapshot.complete` by then
with "handshake deadline expired", `retryable` true. The desktop client
runs the same deadline on its side.

### 12.3 Caps

- The station caps each inbound message and frame at 1 MiB
  (`StationServer::kMaxIncomingMessageBytes`), the client at 8 MiB
  (`StationClient::kMaxIncomingMessageBytes`). Both are applied to the
  socket before any frame is read.
- `media.control` messages are capped at 128 KiB and `station.metrics.v1`
  at 16 KiB, when encoded and when decoded.
- The station accepts at most 8 connections at once
  (`kMaxConcurrentPeers`), counting those still connecting. The next one
  gets `session.end` "Station is at its concurrent-connection limit",
  `retryable` true, because a reconnecting client meets it while its own
  dead sockets drain.

### 12.4 Ending, preemption and retryable

`session.end` carries a `reason` and `retryable`. `auth.result` carries
`retryable` too. A client redials only after a retryable end; after one
that is not retryable it stops and tells the operator.

| Cause | Message | `retryable` |
| --- | --- | --- |
| Wrong token | `auth.result` accepted false, "Authentication failed", then the station closes | false |
| Token checks locked out (section 3.3) | `auth.result` accepted false, "Too many failed authentication attempts; try again later" | true |
| Different major | `session.end` naming both versions | false |
| Message the station cannot decode (section 13) | `session.end` "undecodable message" | false |
| Out-of-order handshake (section 5.1) | `session.end` | false |
| Preempted by a newer authenticated connection | `session.end` "Displaced by a newer authenticated connection from ..." | false |
| Connection limit reached | `session.end` | true |
| Connect deadline expired | `session.end` | true |
| Heartbeat timeout | `session.end` | true |
| Station shutting down | `session.end` "station shutting down" | true |

Only one session is authenticated at a time. A second connection that
authenticates takes the session: the station ends the first with
`retryable` false, so the two clients do not trade the radio back and
forth, and the displaced operator reconnects by hand.

The desktop client redials on the schedule 1, 2, 5, 10, 30 and 60 s, then
stays at 60 s (`kReconnectBackoffSteps` in `StationClient.cpp`), and starts
the schedule over after a connection that reached `snapshot.complete` (and,
with media, after the media connection is established).

## 13. Unknown message kinds

The two ends treat a kind they do not know differently:

- **A client** logs a message it cannot decode, or a kind meant for the
  other direction, and ignores it (`StationClient::onTransportText`). A
  newer station's new kind therefore costs an older client nothing.
- **The station** ends the connection when it cannot decode a message: an
  unknown `type`, a missing or mistyped required key, or an oversized
  `media.control`. It sends `session.end` "undecodable message" with
  `retryable` false (`StationServer::onTransportText`). A known kind meant
  for the other direction is logged and ignored.

So a client never sends a message kind, or a verb or property the station
has not advertised (section 6.2). An unknown verb does not end the
connection (section 9.2), but the gate keeps a client from relying on
that.

## 14. LAN announcement

A station that listens announces itself on its local networks
(`StationLanAnnouncement.h`, `StationLanAnnouncer.cpp`):

- UDP to port 47910, to the multicast groups 239.255.42.99 (IPv4) and
  `ff12::4e52:5344` (IPv6), from one source address of each family on each
  eligible interface, with a multicast hop limit of 1 and multicast
  loopback off;
- once when it starts, then every 5 s (`kStationLanAnnouncementIntervalMs`
  5000); a listener forgets a station it has not heard for 15 s
  (`kStationLanCacheTtlMs`);
- at most 512 bytes (`kStationLanMaxDatagramBytes`).

The datagram is binary, schema 1, in this order:

| Field | Size | Value |
| --- | --- | --- |
| Magic | 4 bytes | ASCII `NRSC` |
| Schema | 1 byte | 1 (`kStationLanAnnouncementSchema`) |
| Service | 1 byte | 1: the control WebSocket over TLS (`kStationLanWssControlService`) |
| Control port | 2 bytes | big-endian, not 0 |
| Pin | 95 bytes | the certificate pin (section 3.2), uppercase |
| Core name length | 1 byte | 1 to 128 |
| Core name | that many bytes | UTF-8, no control characters |
| Radio connected | 1 byte | 0 or 1 |
| Radio name length | 1 byte | 0 to 128; at least 1 when the radio is connected |
| Radio name | that many bytes | UTF-8, no control characters |
| Radio MAC | 17 bytes | uppercase hex pairs joined by colons; `00:00:00:00:00:00` only when no radio is connected |

A listener refuses a datagram with another magic, schema or service, any
bytes left over, or a field that fails these rules. It dials
`wss://<source address>:<control port>`, with the IPv6 scope when the
address is link-local, and pins the announced pin.

The conformance vector `media/lan-announcement.bin` (section 16.4) is a
datagram the station's own encoder wrote, with its decoded fields in
`media/lan-announcement.expect.json`. `tst_link_conformance_media` decodes
it and encodes the fields again, so a change to this layout fails there
until the vector, and this table, move with it.

## 15. Limits

The limits both ends keep. Three come from constants the surface test
cannot link, so `tst_link_surface_manifest` reads their source lines back:
`kMaxEndpoints` (8) in `src/core/session/media/DaemonMediaController.cpp`;
the endpoint width minimum 1 and the frame rate bounds 1 and 60, literals in
the subscribe check `exactInt(...)` in the same file (the frame rate
maximum also equals `kMaximumSpectrumDisplayFramesPerSecond` in
`DisplayBudget.h`); and `kReconnectBackoffSteps` in
`src/core/session/StationClient.cpp`, in units of
`StationClient::kDefaultReconnectBackoffUnitMs` (1000 ms).

<!-- surface:limits -->
<!-- Generated by scripts/render-link-tables.py from tests/data/link/v1/surface.json. Do not edit by hand. -->

| Limit | Value | Unit | Source in the code |
| --- | --- | --- | --- |
| `clientInboundMessageBytes` | 8388608 | bytes | StationClient::kMaxIncomingMessageBytes |
| `clientReconnectBackoffMs` | 1000, 2000, 5000, 10000, 30000, 60000 | ms | StationClient.cpp kReconnectBackoffSteps x StationClient::kDefaultReconnectBackoffUnitMs |
| `connectDeadlineMs` | 30000 | ms | kStationHandshakeDeadlineMs |
| `deltaFlushMs` | 50 | ms | StationServer::kDefaultDeltaFlushMs |
| `endpointFps` | 1 to 60 | frames per second | DaemonMediaController.cpp handleSubscribe literal 1; kMaximumSpectrumDisplayFramesPerSecond |
| `endpointPixels` | 1 to 4096 | pixels | DaemonMediaController.cpp handleSubscribe literal 1; SpectrumEndpoint::kMaxPixels |
| `heartbeatIntervalMs` | 20000 | ms | StationServer::kDefaultHeartbeatIntervalMs |
| `maxDisplayEndpoints` | 8 | count | DaemonMediaController.cpp kMaxEndpoints |
| `maxPeers` | 8 | count | StationServer::kMaxConcurrentPeers |
| `mediaControlBytes` | 131072 | bytes | kMaxMediaControlBytes |
| `missedPongs` | 2 | count | StationServer::kDefaultMaxMissedPongs |
| `stationInboundMessageBytes` | 1048576 | bytes | StationServer::kMaxIncomingMessageBytes |
| `telemetryBytes` | 16384 | bytes | kMaxStationTelemetryBytes |

<!-- /surface -->

## 16. Conformance

`tests/data/link/v1/` holds the machine-readable half of this document.
Both the station's tests and the app's tests read it; nothing in it is
bundled into the app. The station's runners are
`tst_link_conformance_control`, `tst_link_conformance_session` and
`tst_link_conformance_media`, over the shared loader, placeholder matcher
and script player in `tests/LinkFixtures.{h,cpp}`. The app runs the same
files against its own client.

### 16.1 The files

- `surface.json` is the link's surface: the nine sections this document's
  tables render (`messageKinds`, `capabilities`, `mirrorClasses`,
  `objectKeys`, `commands`, `settingsScope`, `telemetry`, `mediaControl`,
  `limits`). `tst_link_surface_manifest` captures it from the code and
  fails when the committed file differs; `tst_link_surface_manifest_regen`
  writes it again (`NEREUS_LINK_REGEN_OUT=tests/data/link/v1`). Each
  `capabilities` entry carries the `value` a station with every feature on
  sends; section 6.3 is rendered from it.
- `manifest.json` lists the fixtures:
  `{"linkMajors":[1],"fixtures":[{"id":"<fixture id>","file":"<path under v1/>","kind":"control"|"session"|"media","requires":{"<feature>":<version>}}]}`.
  `requires` names the capability versions a fixture exercises, and is
  `{}` for a fixture every major-1 station passes. Every file under
  `control/`, `sessions/` and `media/` is listed once; a media entry names
  its `.bin`, and its `.expect.json` sits beside it.
- `control/*.json`: `{"from":"station"|"client","wire":{<the exact message>},"decodes":true|false}`.
- `sessions/*.json`: `{"stationSetup":{<the fake radio model and settings>},"steps":[<steps>]}`,
  where a step is `{"from":"station"|"client","message":{<a message>}}`,
  `{"advanceMs":N}` or `{"expectClosed":{"retryable":true|false}}`.
- `media/*.bin` with `*.expect.json`: the bytes of one packet exactly as
  it travels, and `{"codec":<codec>,"expect":{<decoded values>}}`, where
  `<codec>` is `nsdc1`, `ps3d`, `opus` or `nrsc1` (the LAN announcement of
  section 14). `expect` may hold `"after": ["<fixture id>", ...]` for a
  codec whose decoder keeps state (section 16.4).

Expected messages may hold placeholders: `"$any"` (any value, the key must
be present), `"$string"`, `"$int"` (a whole number), `"$capture:<name>"`
(records the value) and `"$ref:<name>"` (must equal a recorded value).
Objects must have the same keys and arrays the same length; numbers
compare by value, so `1` and `1.0` are equal.

### 16.2 Control fixtures

Each end decodes `wire`; when `decodes` is true it encodes the result
again and the two JSON objects compare equal after parsing, key order
ignored. The fixtures cover every message kind in each direction it
travels: seven from the client (`hello`, `auth.request`, `command.invoke`,
`media.control`, `property.write`, `settings.write`, `settings.remove`)
and sixteen from the station, with a `delta` carrying `"nan"` and `"-inf"`
(section 4.2). The refusals are: a `media.control` over its 128 KiB cap
and a `station.metrics.v1` over its 16 KiB cap (each an otherwise valid
message padded past the cap), a missing required key (`command.invoke`
without `id`), a wrong type (an `f64` entry holding `true`), an `f64`
string other than the three of section 4.2, a `hello` major above 65535,
and an unknown `type`.

The two transport caps (1 MiB into the station, 8 MiB into the desktop
client, section 12.3) are enforced by the WebSocket layer before any
message is decoded, so a decoder fixture cannot hold them; `surface.json`
records them under `limits`.

### 16.3 Session fixtures

The station's player builds the station `stationSetup` describes, opens a
client connection to it over an in-process transport, and walks the
steps. It sends each client message and matches each station message
against the next one the station sent, in arrival order. The app's runner
does the reverse: it sends the station's messages to its client and
matches what the client sends.

- **The token.** `"$ref:token"` in a client message is the station's
  token. The player reads it from the station at run time; no fixture
  holds a token.
- **Time.** Time moves only through `{"advanceMs":N}`. The station's
  player keeps a virtual clock over the station's own timers (the connect
  deadline, the heartbeat and the 50 ms delta flush) and fires each when
  its virtual time comes. A `delta` that waits for the flush follows an
  `{"advanceMs":50}` step. No runner sleeps.
- **Closing.** `{"expectClosed":{"retryable":R}}` holds when the station
  has closed the connection, every message it sent before closing is
  listed in the steps, and `R` equals `retryable` of the last
  `session.end` or `auth.result` it sent. An authentication refusal sends
  `auth.result` and closes with no `session.end` (section 12.4).
- **The end.** A fixture that does not end in `expectClosed` ends with the
  connection open. Messages the station sends after the last step are not
  checked: on `connect-connectable` the radio keeps sending deltas.
- **The heartbeat.** Pings and pongs are WebSocket frames, not messages
  (section 12.1), so no step holds one. The in-process transport answers a
  ping the way a WebSocket stack does. `heartbeat-answered` passes three
  heartbeat intervals with the link up; `heartbeat-missed` has a client
  that never answers, and the station ends the session on the third
  interval with `heartbeat timeout`, `retryable` true.
- **Summarised snapshots.** Outside `connect-connectable`, a `schema`
  message's `fields` and an `object.create` message's `properties` are
  `"$any"`: `connect-connectable` and `surface.json`'s `mirrorClasses`
  hold their content, and a fixture about something else does not repeat
  it. An end that plays the station's side sends, for such a message, the
  class's fields from `mirrorClasses` and one entry of each field's kind.

`stationSetup` holds:

| Key | Meaning | Default |
| --- | --- | --- |
| `radio` | `"static"`: a model reporting a connected Hermes Lite 2 (MAC `AA:BB:CC:DD:EE:01`) with no radio behind it, so nothing changes on its own; `"connectable"`: a model connected to the fake Protocol 1 radio, receive processing and all | `"static"` |
| `slices` | slices before the client connects | 1 |
| `panadapters` | panadapters before the client connects | 0 |
| `coreAccessories` | the Core owns its accessories: the `tuner`, `amplifier` and `rfkit` objects and the accessory commands | false |
| `stationTci` | the Core runs a station TCI server (it stays off) | false |
| `stepAttenuator` | a step attenuator controller is bound, so the radio hardware objects are offered | false |
| `media` | media is enabled | false |
| `priorFailedAuthentications` | other clients that each sent a wrong token before this one connects | 0 |
| `clientAnswersPings` | the client's transport answers the station's pings | true |
| `preemptingClient` | `{"afterStep": i}`: a second client authenticates once step `i` is done | none |

The station runner starts every fixture from an empty settings profile,
and the bundled NR3 model files count as absent, so a fixture reads the
same on every machine.

| Fixture | What it holds the station to |
| --- | --- |
| `connect-connectable` | The whole connect sequence to `snapshot.complete` on a connected radio with one slice, every message in full |
| `wrong-token` | `auth.result` refused, `retryable` false, then the close |
| `lockout` | After five wrong tokens from other clients, the right token is refused as rate limited, `retryable` true |
| `major-refused` | A `hello` with major 2 gets `session.end` naming both versions, `retryable` false |
| `lower-minor` | A `hello` with minor 4 agrees minor 4: the capabilities without the minor-11 entries, and a minor-11 verb refused with a plain reason |
| `preempted` | A second authenticated client ends this session: `session.end`, `retryable` false |
| `heartbeat-answered`, `heartbeat-missed` | The heartbeat, above |
| `connect-deadline` | No `auth.request` within 30000 ms: `session.end` "handshake deadline expired", `retryable` true |
| `property-write` | A write and its `property.result` and side-effect `delta`; a refused outbound property and an unknown one; a write without a `writeId` answered by `delta`; a write to a slice's signal strength refused as outbound |
| `settings-write` | A station-scoped write echoed with its origin; an operator-local write rejected; a removal sent as `settings.value` with no entry |
| `unknown-verb` | `command.result` refused, "unrecognised command verb"; the connection stays up |
| `unknown-kind` | `session.end` "undecodable message", `retryable` false |
| `verbs-*` | Each verb in `commands`, grouped by the capability that gates it, invoked with its own arguments and, where it takes any, with one argument renamed |

`tst_link_conformance_session` also checks that every verb in the
`commands` table is invoked both ways by some fixture.

### 16.4 Media vectors

`tst_link_conformance_regen` writes every vector from the station's own
encoders and fixed inputs, into `NEREUS_LINK_REGEN_OUT` only. Each `.bin`
is one packet as it travels.

**Decoding a vector.** A runner decodes a vector on a fresh decoder. When
`expect` holds `"after": ["<fixture id>", ...]`, it first decodes each
named vector's bytes, in order, on that same decoder, without checking
their own expectations, then decodes the vector itself and compares only
its own expectation. A vector without `after` decodes on a fresh decoder
alone. Loss is a packet left out of `after`: `nsdc1-keyframe-after-loss`
is decoded after `nsdc1-full` only, with the delta that came between
omitted. An `after` that names the vector itself, a missing vector, a
vector of another codec, or that forms a cycle through the vectors it
names, is a malformed vector, and the runner reports it.

The station's media runner decodes the bytes and compares the result with
`expect`. Where the encoder is exact (`nrsc1`, `ps3d`, `nsdc1`) it also
holds the encoder to the bytes: it encodes `expect` (or, for `nsdc1`, the
regen target's fixed input frames) again and compares. Opus is not held
to its bytes, because its floating-point encoder may differ between
processors; its vectors hold decoders to the reference PCM instead.

| Codec | Vector | After | Decoded values |
| --- | --- | --- | --- |
| `nrsc1` | `lan-announcement`: one LAN announcement datagram (section 14) | none | `controlPort`, `fingerprint`, `coreName`, `radioName`, `radioMac`, `radioConnected`, exact |
| `ps3d` | `ps3d-frame`: one PureSignal display chunk, eight points and four correction points | none | Every header field and the eight value lists; `tolerance` `{"absolute": 0}`, because the values travel as IEEE-754 binary64 |
| `nsdc1` | `nsdc1-full`: frame 1, a keyframe | none | `disposition` `accepted`, `reason` `none`, `keyframe` (the header's keyframe flag), the context (`endpointId`, `contextGeneration`, `minDbm`, `maxDbm`), `encoderSequence`, `producerTimestamp`, `waterfallAdvance` and the reconstructed `traceDbm`, `waterfallDbm` and `wideDbm` rows; `tolerance` `{"dbm": 0.01}` |
| `nsdc1` | `nsdc1-delta`: frame 2, a delta | `nsdc1-full` | As above, `keyframe` false |
| `nsdc1` | `nsdc1-delta-after-loss`: frame 3, a delta, when frame 2 was lost | `nsdc1-full` | `disposition` `needKeyframe`, `reason` `sequenceGap`, no frame |
| `nsdc1` | `nsdc1-keyframe-after-loss`: frame 4, the keyframe the sender was asked for | `nsdc1-full` | `accepted`, `keyframe` true, the frame |
| `opus` | `opus-1` to `opus-4`: four consecutive RTP packets at the station's settings (48 kHz stereo, 1920 samples per packet, 24000 bit/s, wideband) | the packets before it | `status` `accepted`, `sequence`, `timestamp`, `channels` 2, `bandwidth` 1103 (Opus wideband), `samplesPerChannel` 1920, and `pcm16`, the station decoder's output as 16-bit values (`round(sample * 32767)`); `ssrc` is the packets' RTP source, which the decoder is given; `tolerance` `{"minSnrDb": 60}` |

An NSDC `dbm` tolerance applies to every number of the decoded frame. An
Opus tolerance is either `{"minSnrDb": N}` (the decoded PCM is at least N
dB above its difference from `pcm16`) or `{"lsb16": N}` (no sample is more
than N 16-bit steps from `pcm16`); the vector states which.

### 16.5 Running the station's runners

```
cmake --build build --target tst_link_conformance_control tst_link_conformance_session tst_link_conformance_media
QT_QPA_PLATFORM=offscreen ctest --test-dir build -R '^tst_link_conformance_(control|session|media)$' --output-on-failure
```

Each runner also alters one of its fixtures in memory and checks that the
failure names the step or field that differs; the media runner also checks
that a malformed `after` is reported. With
`NEREUS_LINK_TRACE_DIR` set, `tst_link_conformance_session` writes every
message the station sent in each fixture to `<id>.jsonl` there, which is
how a fixture is written to what the code does.

## 17. Changing the link

- **Anything on the wire changes with its documents, in the same commit.**
  A change that adds or alters a message, key, capability, class,
  property, verb, settings rule, telemetry field, media operation or limit
  updates this document, `tests/data/link/v1/` and `surface.json` together
  (regenerate with `tst_link_surface_manifest_regen`, then
  `python3 scripts/render-link-tables.py`), and the conformance runners pass
  on both ends.
- **New features get capability versions, not minors.** Each new feature
  gets its own `<feature>Version` in `StationCapabilities`, following
  `notchControlVersion` and `audioProfileVersion`. `kSessionProtocolMinor`
  stays 11.
- **Nothing unadvertised.** A client never sends a message kind or verb
  the station has not advertised, because an older station ends the
  connection on anything it cannot decode (section 13); an older client
  logs and ignores an unknown kind. Older peers see exactly today's wire.
- **The transmit boundary stays at the station.** No change lets a remote
  client key the radio outside the station's gates: transmit disabled
  until `snapshot.complete`, the watchdog, the starvation deadline, the
  time-out, the unkey-confirmed gate, `TxInterlockPolicy`, PA protection
  and SWR gating. A reconnect, a snapshot replay, a path change or a
  property write never keys.
- **Operator wording.** A reason a client may show an operator is plain
  English with no protocol terms.
