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
| Accessory control (the `tuner`, `amplifier`, `rfkit`, `stationTci`, `accessoryData` and `accessorySettings` objects, the 4O3A, RF-Kit, station TCI, accessory record and device settings commands and refusals) | [2026-09-23-remote-accessory-control-v1.md](2026-09-23-remote-accessory-control-v1.md) |

The design authority behind all of them is the
[remote daemon architecture design](2026-07-28-remote-daemon-architecture-design.md),
the [station identity and pairing design](2026-08-02-remote-station-identity-and-pairing-design.md)
and the [R3 plan](2026-09-20-remote-daemon-r3-plan.md).

## 2. Transport

- The station listens with a `QWebSocketServer` in secure mode
  (`StationServer.cpp`, `listen()`), so every connection is a WebSocket
  over TLS (`wss://`). The server starts from Qt's default TLS
  configuration (`QSslConfiguration::defaultConfiguration()`) and sets the
  minimum explicitly: `setProtocol(QSsl::TlsV1_2OrLater)`, TLS 1.2 or later,
  whatever Qt's default becomes. `tst_link_version` reads it back from the
  listener (`StationServer::tlsConfiguration()`), and offers the listener a
  client that speaks only TLS 1.1 (at OpenSSL security level 0): the
  station refuses it and serves a TLS 1.2 client. The test first shows
  that same client finishing a TLS 1.1 handshake with a listener of its
  own, which differs from the station's in two settings (protocol TLS 1.0
  or later, and OpenSSL security level 0), so the client can speak TLS
  1.1. What the test does not show is which of the station's two settings
  refuses it: OpenSSL 3 at its default security level refuses TLS 1.0 and
  1.1 whatever the protocol setting says, so on such a build either the
  explicit minimum or the library's default would refuse this client. The
  station does not ask for
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
- not before: one day before creation (`kNotBeforeSkewSeconds`,
  `-60 * 60 * 24` seconds);
- not after: `60 * 60 * 24 * 365 * 10` seconds after creation, which is
  3650 days (`kValiditySeconds`, "10 years"); both are offsets from the
  moment of creation (`X509_gmtime_adj`), so the whole validity span is
  3651 days;
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
decoder refuse the message) and the keys it may carry, each with the JSON
type the station writes it as. The key sets are every key
`SessionMessages::encode` can write for the kind, conditional ones
included: `tst_link_surface_manifest` reads the encoder's source and fails
when a key it can write is missing here, or a key here is one it never
writes.

<!-- surface:messageKinds -->
<!-- Generated by scripts/render-link-tables.py from tests/data/link/v1/surface.json. Do not edit by hand. -->

| Kind (`type`) | Required keys (JSON type) | Optional keys (JSON type) |
| --- | --- | --- |
| `auth.request` | `token` (string), `type` (string) | none |
| `auth.result` | `accepted` (boolean), `reason` (string), `type` (string) | `retryable` (boolean) |
| `capabilities` | `properties` (array), `type` (string) | none |
| `command.invoke` | `args` (array), `id` (number), `type` (string), `verb` (string) | none |
| `command.result` | `accepted` (boolean), `affected` (array), `id` (number), `reason` (string), `type` (string), `verb` (string) | `values` (array) |
| `delta` | `key` (string), `properties` (array), `type` (string) | none |
| `hello` | `major` (number), `minor` (number), `peer` (string), `settingsSchema` (number), `type` (string) | `features` (object), `majors` (array) |
| `media.control` | `payload` (object), `type` (string) | none |
| `object.create` | `class` (string), `key` (string), `properties` (array), `type` (string) | none |
| `object.destroy` | `class` (string), `key` (string), `type` (string) | none |
| `property.result` | `key` (string), `results` (array), `type` (string), `writeId` (number) | none |
| `property.write` | `key` (string), `properties` (array), `type` (string) | `writeId` (number) |
| `schema` | `class` (string), `fields` (array), `type` (string) | none |
| `session.end` | `reason` (string), `type` (string) | `retryable` (boolean) |
| `settings.reject` | `key` (string), `properties` (array), `type` (string) | `reason` (string) |
| `settings.remove` | `key` (string), `properties` (array), `type` (string) | none |
| `settings.snapshot` | `properties` (array), `type` (string) | none |
| `settings.value` | `key` (string), `origin` (string), `properties` (array), `type` (string) | none |
| `settings.write` | `key` (string), `origin` (string), `properties` (array), `type` (string) | none |
| `snapshot.complete` | `type` (string) | none |
| `station.metrics.v1` | `payload` (object), `type` (string) | none |

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
   It names every link major the station supports (section 6.1), so a
   client can pick one, or leave without having sent its token. The one
   exception: a station already holding its limit of connections (8,
   `kMaxConcurrentPeers`, section 15) sends no `hello`. The first and only
   message on the new connection is `session.end` "The Core already has
   as many connections as it allows. Try again shortly.", `retryable`
   true, and the station closes it. A client handles a `session.end` in
   place of the `hello` as it would any other (fixture
   `connection-limit`).
3. The client answers with its own `hello`, naming the major it chose,
   then `auth.request` carrying the token (`StationClient.cpp`, after its
   pin check).
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
in `settingsSchema` is logged by both ends and is not a refusal. It may
also carry `majors` and `features` (sections 6.1 and 6.2); the station's
`hello` always carries both.

The station refuses, with `session.end` and `retryable` false: a second
`hello` ("This app started connecting twice on one connection."),
`auth.request` before `hello` or a second `auth.request` ("This app sent its
pairing token out of order."), and any other message before authentication
("This app sent a request before the Core had accepted its pairing
token.").

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

Both ends send a major and a minor in `hello`. The minor is
`kSessionProtocolMinor` 11 (`SessionMessages.h`) and stays there: a
feature added since carries its own capability version (section 6.3), and
one the station must know about before capabilities are sent is declared
in `features` (section 6.2).

**Majors, both ways.** Each end supports its own major and the one before
it (`kSupportedSessionMajors` in `LinkVersion.h`; today `[1]`, since there
is no major before 1), and the two ends agree the highest major both
support (`LinkVersion::agreeMajor`). Only ends two or more majors apart
share none.

- `majors` in `hello` is the sender's supported majors, oldest first, an
  array of whole numbers from 0 to 65535, never empty. A `hello` without it
  stands for `[major]`, which is what every peer built before the key
  existed sends.
- The station's `hello` has `major` set to the **oldest** major it supports
  and `majors` to its whole list. A client built before `majors` existed
  reads only `major` and leaves unless it is the one major it speaks, so
  the oldest is the value every client the station can still serve
  accepts. A client that reads `majors` ignores `major`. For example a
  station supporting `[1, 2]` sends `"major": 1, "majors": [1, 2]`.
- The client picks the highest major in both its list and the station's,
  and sends it as `major` in its own `hello`, with its own list as
  `majors`. The station accepts a client `major` that is in its own list,
  and the session runs at that major. With no shared major, the desktop
  client disconnects without sending its `hello` or its token and without
  retrying, and shows the station's wording (below).
- The station refuses any other `major` with `session.end`, `retryable`
  false, and a reason naming both sides' newest versions and the side to
  update (`SessionEndReasons::versionRefused(station majors, client
  majors)`), for example "This Core runs link version 1 and this app runs
  version 3. Update the Core." The reason is plain words and an app shows
  it as sent.
- An equal major agrees the lower of the two minors
  (`std::min(kSessionProtocolMinor, message.protocolMinor)`), and each end
  keeps to what the agreed minor allows.

| Station supports | App supports | Agreed |
| --- | --- | --- |
| `[1]` | `[1]` | 1 |
| `[1, 2]` | `[2, 3]` | 2 |
| `[3, 4]` | `[2, 3]` | 3 |
| `[2, 3]` | `[1]` | none: refused, "Update this app." |
| `[1]` | `[2, 3]` | none: refused, "Update the Core." |

A second major does not exist yet. The station and the desktop client take
their lists as a constructor argument, so the negotiation is tested with
injected lists (`tst_link_version`), and a debug build of `nereusd` takes
`--test-link-majors <list>` (for example `1,2`), which replaces the list
the station advertises and accepts, so an app's version screens can be
tried against a real station. A release build of `nereusd` refuses to
start with that option: "--test-link-majors works only in a debug build of
nereusd."

**Declared features.** `features` in `hello` is an object from a feature
name to a whole-number version from 0 to 2147483647; names are not empty.
A `hello` without it declares none. A receiver ignores a name it does not
know. What the station must know about the app before capabilities are
sent (device authentication, pairing, the takeover question, Setup
descriptions) is declared here, and asked with
`StationServer::peerDeclares(peer, feature, minVersion)`; the desktop
client asks `StationClient::stationDeclares(feature, minVersion)`. Neither
end declares a feature yet, so each sends `{}`. A client never sends a
message kind or verb the station has not advertised, in `features` or in
its capabilities.

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
| `stationTelemetryVersion` | 4 |
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
| `radioHardwareVersion` | 4 |
| `remotePgxlControlVersion` | 4 |
| `remoteRfKitControlVersion` | 4 |
| `stationTciVersion` | 1 |
| `accessoryDataVersion` | 2 |
| `remoteTgxlControlVersion` | 4 |
| `transmitSettingsVersion` | 7 |

<!-- /surface -->

When a feature is off, its version is 0:

- `remoteMediaVersion`, `remoteWidebandDisplayVersion`,
  `remoteAudioStatusVersion`, `spectrumGrantVersion`, `audioProfileVersion`,
  `audioClockVersion`, `receiverAudioVersion`, `headphonesMixVersion`: 0
  unless media is enabled.
- `remoteDisplayBudgetVersion`: 0 unless media and budget enforcement are
  on and a budget has been computed.
- `stationTelemetryVersion`: 0 unless telemetry is enabled. At 4 the
  radio section also carries, for a peer at agreed minor 11, the Core's
  PA readings and radio link quality (section 10).
- `remoteTgxlConfigVersion`, `remoteFourO3AControlVersion`: 0 unless the
  Core owns its accessories.
- `wdspVersion`, `wdspCompatibilityVersion`, `nnrVersion`,
  `psAlgorithmVersion`, `dspAssetVersion`: 0 on a station built without
  WDSP. `psDisplayVersion` also needs media.
- `remoteCtunVersion`, `propertyResultVersion` and `notchControlVersion`
  are never 0.
- `radioHardwareVersion`: sent only at agreed minor 11. 0 without the step
  attenuator bound; 1 with it; 2 with the Alex antennas too; 4 with the HL2
  I/O board too: the `ioBoard` object, `setAlexRxAntenna` (which needs 3)
  and the filter policy command `setAlexBpfMode` (which needs 4). A station
  no longer sends 3; a client compares the version as a minimum
  (section 6.2), so 4 serves `setAlexRxAntenna` too.
- `remotePgxlControlVersion`, `remoteRfKitControlVersion`,
  `remoteTgxlControlVersion`: sent only at agreed minor 11, and 0 unless
  the Core owns its accessories. `remotePgxlControlVersion` 3 adds the
  Power Genius's own settings, and 4 its OPERATE and STANDBY, the Core's
  Scan LAN for it and its saved address (`setPgxlOperate`, `scanPgxlLan`,
  `setPgxlAddress`, section 9.1, parity Task 9), and `remoteTgxlControlVersion` 1 the Tuner
  Genius's (the `accessorySettings` object and the device settings
  commands, section 9.1); `remoteTgxlControlVersion` 2 adds the Tuner
  Genius's antenna, operate and bypass (`setTgxlAntenna`,
  `setTgxlOperate`, `setTgxlBypass`, section 9.1), 3 has
  `setTgxlOperate` with `on` true put the tuner in operate whole (bypass
  off and operate on, from the one command), and 4 adds the relay nudge,
  the Core's Scan LAN and the saved address (`moveTgxlRelay`,
  `scanTgxlLan`, `setTgxlAddress`, section 9.1, parity Task 8);
  `remoteRfKitControlVersion` 3 adds `resetRfKitError`, the RF-Kit
  amplifier's Reset amp error, and 4 its OPERATE and STANDBY, antenna,
  TCI mode and saved address (`setRfKitOperate`, `setRfKitAntenna`,
  `setRfKitTciMode`, `setRfKitAddress`, section 9.1, parity Task 10).
- `transmitSettingsVersion`: sent only at agreed minor 11, and 0 on a
  station with no radio model. At 1 a receive-only Core takes a
  `property.write` on `transmit` of any property except the keying set
  (`mox`, `tune`, `voxEnabled`, `twoToneActive`), and a `settings.write`
  or `settings.remove` of a DSP > Options TX key
  (`DspOptions<Setting><Mode>Tx`), while its radio is off the air, and
  applies it at once (a DSP > Options TX key reaches the Core's TX channel
  when the TX slice's mode is in its group). Version 1 covers the
  `transmit` properties mirrored today: `power`, `micGain`, `filterLow`,
  `filterHigh`, `lineInGain`, `userDigOut`, `pureSig`,
  `forceAttwhenPSAoff`, `forceAttwhenPowerChangesWhenPSAon`,
  `forceAttwhenPowerChangesWhenPSAonAndDecreased`, `antiVoxTauMs`,
  `antiVoxRun` and `paSettingsBypass`. At 2 it also covers the TX and
  Phone/CW applets' settings on `transmit`: `tunePower`, `voxThresholdDb`,
  `voxHangTimeMs`, `monEnabled`, `monitorVolume`, `txLevelerOn`,
  `txEqEnabled`, `cfcEnabled`, `cpdrOn`, `cpdrLevelDb`, `amCarrierLevel`,
  `dexpEnabled` and `micGainDb`, each refused outside its range with the
  range in plain words (section 7.3); the read-only `tunePowerForTxBand`
  and `tuneDrivePowerSource`; and the command `setTunePowerForTxBand`
  (section 9.1). At 3 it also covers the radio's microphone input on
  `transmit` (Setup > Audio > TX Input): `micBoost`, `micXlr`,
  `micTipRing`, `micBias`, `micPttDisabled`, `lineIn` and `lineInBoost`,
  `lineInBoost` refused outside its range (section 7.3); the Core's TX
  profiles, the read-only `activeTxProfile` and `txProfilesJson`; and the
  commands `txProfile.select`, `txProfile.save`, `txProfile.delete` and
  `rade.resetVocoder` (section 9.1). At 4 it also covers the TX EQ, CFC,
  phase rotator, CESSB, leveler and ALC settings on `transmit` (the TX EQ
  and CFC dialogs, Setup > DSP > CFC and AGC/ALC's TX Leveler and TX ALC):
  `txEqUseLegacy`, `txEqPreamp`, `txEqBandsJson`, `txEqFreqsJson`,
  `txEqNc`, `txEqMp`, `txEqCtfmode`, `txEqWintype`, `txEqParaEqData`,
  `cfcCompressionJson`, `cfcEqFreqJson`, `cfcPostEqBandGainJson`,
  `cfcPostEqEnabled`, `cfcPostEqGainDb`, `cfcPrecompDb`, `cfcParaEqData`,
  `phaseRotatorEnabled`, `phaseRotatorFreqHz`, `phaseRotatorStages`,
  `phaseReverseEnabled`, `cessbOn`, `txLevelerMaxGain`, `txLevelerDecay`,
  `txAlcMaxGain` and `txAlcDecay`, each refused outside its range and a
  band array refused whole (section 7.3). At 5 it also covers Setup >
  Transmit > Power, Transmit > DEXP/VOX and Test > Two-Tone IMD: on
  `transmit`, `tuneDrivePowerSource` becomes two-way, and
  `powerByBandJson`, `tunePowerByBandJson`, `dexpAttackTimeMs`,
  `dexpDetectorTauMs`, `dexpExpansionRatioDb`, `dexpHighCutHz`,
  `dexpHysteresisRatioDb`, `dexpLookAheadEnabled`, `dexpLookAheadMs`,
  `dexpLowCutHz`, `dexpReleaseTimeMs`, `dexpSideChannelFilterEnabled`,
  `antiVoxGainDb`, `twoToneFreq1`, `twoToneFreq2`, `twoToneLevel`,
  `twoTonePower`, `twoTonePulsed`, `twoToneInvert`, `twoToneFreq2Delay`
  and `twoToneDrivePowerSource`, each refused outside its range and a band
  map refused whole (section 7.3); on `stepAtt`, `attOnTxEnabled`,
  `attOnTxValue` and `forceAttWhenPsOff`; and the Power page's SWR
  Protection and External TX Inhibit keys (`SwrProtectionEnabled`,
  `SwrProtectionLimit`, `SwrTuneProtectionEnabled`, `TunePowerSwrIgnore`,
  `WindBackPowerSwr`, `TxInhibitMonitorEnabled`,
  `TxInhibitMonitorReversed`), taken while the radio is off the air
  (section 8), the SWR Protection keys applied to the Core's SWR
  protection at once. At 6 it also covers Setup > PA: the PA profiles
  (`hardware/<mac>/pa/...`: PA Gain's profiles, per-band gains, adjust
  matrix and max power) and the PA forward-power table
  (`hardware/<mac>/paCalibration/...`: the Watt Meter page), taken while
  the radio is off the air (section 8) and applied to the Core's PA
  profiles and calibration at once. PA Gain's auto-calibrate sweep keys
  the radio and waits for remote transmit. At 7 it also covers PureSignal
  arming: the commands `ps3.single`, `ps3.automatic`, `ps3.applyCurrent`
  and `ps3.restoreCorrection` (section 9.1), and a `property.write` on
  `pureSignalSettings`, which the Core then applies to its PureSignal at
  once instead of only keeping it (section 7.3). Arming keys nothing, so a
  receive-only Core permits PureSignal: its `pureSignal` object's
  `canActuate` says whether PureSignal is ready on the Core's radio, not
  whether this peer may transmit (`txPermitted`). `ps3.twoTone` with
  `enabled` true keys the radio and waits for remote transmit. Each is
  refused while the radio is on the air (section 7.3). The keying set stays refused on a receive-only
  Core, on and off the air, and so do raw settings writes of
  `hardware/<mac>/tx/...`, `powerByBand` and `tunePowerByBand` (the
  `transmit` object owns them). A window whose Core sends 0 keeps its
  transmit settings unavailable. A peer below agreed minor 11 is never
  offered it, and a receive-only Core refuses its transmit writes and DSP >
  Options TX keys as before. `transmitSettingsVersion` is the last
  capabilities entry.
- `stationTciVersion`: sent only at agreed minor 11, and 0 unless the Core
  runs a station TCI server.
- `accessoryDataVersion`: sent only at agreed minor 11, and 0 unless the
  Core owns its accessories. At 1 the station sends the read-only
  `accessoryData` object (fault histories, connection counters, the
  transmit interlock and the Power Genius output limit) and accepts
  `setTxInterlockPolicy`, `setPgxlPowerCap` and `clearAccessoryFaults`.
  At 2 (parity Task 10) the object also carries the RF-Kit's connection
  counts (`rfkitConnectedSinceMs`, `rfkitPollsOk`, `rfkitPollsFailed`,
  `rfkitReconnectCount`, `rfkitLastPollMs`), appended after the older
  properties, which keep their ordinals.

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
| 46 | `accessoryDataVersion` | `i64` |
| 47 | `remoteTgxlControlVersion` | `i64` |
| 48 | `transmitSettingsVersion` | `i64` |

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

**AccessoryDataModel** (47 properties)

| Ordinal | Property | Wire kind | Direction | Enum values |
| --- | --- | --- | --- | --- |
| 0 | `faultRevision` | `i64` | outbound |  |
| 1 | `pgxlFaults` | `utf8` | outbound |  |
| 2 | `tgxlFaults` | `utf8` | outbound |  |
| 3 | `rfkitFaults` | `utf8` | outbound |  |
| 4 | `pgxlConnectedSinceMs` | `i64` | outbound |  |
| 5 | `pgxlLastRttMs` | `i64` | outbound |  |
| 6 | `pgxlKeepaliveMissed` | `i64` | outbound |  |
| 7 | `pgxlReconnectCount` | `i64` | outbound |  |
| 8 | `pgxlFramesIn` | `i64` | outbound |  |
| 9 | `pgxlFramesOut` | `i64` | outbound |  |
| 10 | `pgxlBytesIn` | `i64` | outbound |  |
| 11 | `pgxlBytesOut` | `i64` | outbound |  |
| 12 | `pgxlLastFrameMs` | `i64` | outbound |  |
| 13 | `pgxlFaultsSession` | `i64` | outbound |  |
| 14 | `tgxlConnectedSinceMs` | `i64` | outbound |  |
| 15 | `tgxlLastRttMs` | `i64` | outbound |  |
| 16 | `tgxlKeepaliveMissed` | `i64` | outbound |  |
| 17 | `tgxlReconnectCount` | `i64` | outbound |  |
| 18 | `tgxlFramesIn` | `i64` | outbound |  |
| 19 | `tgxlFramesOut` | `i64` | outbound |  |
| 20 | `tgxlBytesIn` | `i64` | outbound |  |
| 21 | `tgxlBytesOut` | `i64` | outbound |  |
| 22 | `tgxlLastFrameMs` | `i64` | outbound |  |
| 23 | `tgxlFaultsSession` | `i64` | outbound |  |
| 24 | `interlockMode` | `enum` | outbound | 0, 1, 2 |
| 25 | `interlockGraceMs` | `i64` | outbound |  |
| 26 | `interlockSwrGateEnabled` | `bool` | outbound |  |
| 27 | `interlockSwrGateMax` | `f64` | outbound |  |
| 28 | `powerCapEnabled` | `bool` | outbound |  |
| 29 | `powerCapW` | `i64` | outbound |  |
| 30 | `powerCapExceeded` | `bool` | outbound |  |
| 31 | `powerCapAlertText` | `utf8` | outbound |  |
| 32 | `powerCapAlertCount` | `i64` | outbound |  |
| 33 | `tuneMemory` | `utf8` | outbound |  |
| 34 | `autoTuneMemoryRecall` | `bool` | outbound |  |
| 35 | `tgxlAntenna1Label` | `utf8` | outbound |  |
| 36 | `tgxlAntenna2Label` | `utf8` | outbound |  |
| 37 | `tgxlAntenna3Label` | `utf8` | outbound |  |
| 38 | `rfkitAntenna1Label` | `utf8` | outbound |  |
| 39 | `rfkitAntenna2Label` | `utf8` | outbound |  |
| 40 | `rfkitAntenna3Label` | `utf8` | outbound |  |
| 41 | `rfkitAntenna4Label` | `utf8` | outbound |  |
| 42 | `rfkitConnectedSinceMs` | `i64` | outbound |  |
| 43 | `rfkitPollsOk` | `i64` | outbound |  |
| 44 | `rfkitPollsFailed` | `i64` | outbound |  |
| 45 | `rfkitReconnectCount` | `i64` | outbound |  |
| 46 | `rfkitLastPollMs` | `i64` | outbound |  |

**AccessorySettingsModel** (21 properties)

| Ordinal | Property | Wire kind | Direction | Enum values |
| --- | --- | --- | --- | --- |
| 0 | `pgxlNickname` | `utf8` | outbound |  |
| 1 | `pgxlBiasMode` | `utf8` | outbound |  |
| 2 | `pgxlFanMode` | `utf8` | outbound |  |
| 3 | `pgxlLedIntensity` | `i64` | outbound |  |
| 4 | `pgxlNetworkKnown` | `bool` | outbound |  |
| 5 | `pgxlDhcp` | `bool` | outbound |  |
| 6 | `pgxlAddress` | `utf8` | outbound |  |
| 7 | `pgxlNetmask` | `utf8` | outbound |  |
| 8 | `pgxlGateway` | `utf8` | outbound |  |
| 9 | `pgxlAnswer` | `utf8` | outbound |  |
| 10 | `pgxlAnswerAccepted` | `bool` | outbound |  |
| 11 | `pgxlAnswerCount` | `i64` | outbound |  |
| 12 | `tgxlNickname` | `utf8` | outbound |  |
| 13 | `tgxlNetworkKnown` | `bool` | outbound |  |
| 14 | `tgxlDhcp` | `bool` | outbound |  |
| 15 | `tgxlAddress` | `utf8` | outbound |  |
| 16 | `tgxlNetmask` | `utf8` | outbound |  |
| 17 | `tgxlGateway` | `utf8` | outbound |  |
| 18 | `tgxlAnswer` | `utf8` | outbound |  |
| 19 | `tgxlAnswerAccepted` | `bool` | outbound |  |
| 20 | `tgxlAnswerCount` | `i64` | outbound |  |

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

**RadioModel** (21 properties)

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
| 19 | `transmitting` | `bool` | outbound |  |
| 20 | `txInhibited` | `bool` | outbound |  |

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

**StepAttenuatorFacade** (18 properties)

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
| 15 | `attOnTxEnabled` | `bool` | bidirectional |  |
| 16 | `attOnTxValue` | `i64` | bidirectional |  |
| 17 | `forceAttWhenPsOff` | `bool` | bidirectional |  |

**TransmitModel** (85 properties)

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
| 15 | `tunePower` | `i64` | bidirectional |  |
| 16 | `voxThresholdDb` | `i64` | bidirectional |  |
| 17 | `voxHangTimeMs` | `i64` | bidirectional |  |
| 18 | `monEnabled` | `bool` | bidirectional |  |
| 19 | `monitorVolume` | `f64` | bidirectional |  |
| 20 | `txLevelerOn` | `bool` | bidirectional |  |
| 21 | `txEqEnabled` | `bool` | bidirectional |  |
| 22 | `cfcEnabled` | `bool` | bidirectional |  |
| 23 | `cpdrOn` | `bool` | bidirectional |  |
| 24 | `cpdrLevelDb` | `i64` | bidirectional |  |
| 25 | `amCarrierLevel` | `i64` | bidirectional |  |
| 26 | `dexpEnabled` | `bool` | bidirectional |  |
| 27 | `micGainDb` | `i64` | bidirectional |  |
| 28 | `tunePowerForTxBand` | `i64` | outbound |  |
| 29 | `tuneDrivePowerSource` | `enum` | bidirectional | 0, 1, 2 |
| 30 | `micBoost` | `bool` | bidirectional |  |
| 31 | `micXlr` | `bool` | bidirectional |  |
| 32 | `micTipRing` | `bool` | bidirectional |  |
| 33 | `micBias` | `bool` | bidirectional |  |
| 34 | `micPttDisabled` | `bool` | bidirectional |  |
| 35 | `lineIn` | `bool` | bidirectional |  |
| 36 | `lineInBoost` | `f64` | bidirectional |  |
| 37 | `activeTxProfile` | `utf8` | outbound |  |
| 38 | `txProfilesJson` | `utf8` | outbound |  |
| 39 | `txEqUseLegacy` | `bool` | bidirectional |  |
| 40 | `txEqPreamp` | `i64` | bidirectional |  |
| 41 | `txEqBandsJson` | `utf8` | bidirectional |  |
| 42 | `txEqFreqsJson` | `utf8` | bidirectional |  |
| 43 | `txEqNc` | `i64` | bidirectional |  |
| 44 | `txEqMp` | `bool` | bidirectional |  |
| 45 | `txEqCtfmode` | `i64` | bidirectional |  |
| 46 | `txEqWintype` | `i64` | bidirectional |  |
| 47 | `txEqParaEqData` | `utf8` | bidirectional |  |
| 48 | `cfcCompressionJson` | `utf8` | bidirectional |  |
| 49 | `cfcEqFreqJson` | `utf8` | bidirectional |  |
| 50 | `cfcPostEqBandGainJson` | `utf8` | bidirectional |  |
| 51 | `cfcPostEqEnabled` | `bool` | bidirectional |  |
| 52 | `cfcPostEqGainDb` | `i64` | bidirectional |  |
| 53 | `cfcPrecompDb` | `i64` | bidirectional |  |
| 54 | `cfcParaEqData` | `utf8` | bidirectional |  |
| 55 | `phaseRotatorEnabled` | `bool` | bidirectional |  |
| 56 | `phaseRotatorFreqHz` | `i64` | bidirectional |  |
| 57 | `phaseRotatorStages` | `i64` | bidirectional |  |
| 58 | `phaseReverseEnabled` | `bool` | bidirectional |  |
| 59 | `cessbOn` | `bool` | bidirectional |  |
| 60 | `txLevelerMaxGain` | `i64` | bidirectional |  |
| 61 | `txLevelerDecay` | `i64` | bidirectional |  |
| 62 | `txAlcMaxGain` | `i64` | bidirectional |  |
| 63 | `txAlcDecay` | `i64` | bidirectional |  |
| 64 | `powerByBandJson` | `utf8` | bidirectional |  |
| 65 | `tunePowerByBandJson` | `utf8` | bidirectional |  |
| 66 | `dexpAttackTimeMs` | `f64` | bidirectional |  |
| 67 | `dexpDetectorTauMs` | `f64` | bidirectional |  |
| 68 | `dexpExpansionRatioDb` | `f64` | bidirectional |  |
| 69 | `dexpHighCutHz` | `f64` | bidirectional |  |
| 70 | `dexpHysteresisRatioDb` | `f64` | bidirectional |  |
| 71 | `dexpLookAheadEnabled` | `bool` | bidirectional |  |
| 72 | `dexpLookAheadMs` | `f64` | bidirectional |  |
| 73 | `dexpLowCutHz` | `f64` | bidirectional |  |
| 74 | `dexpReleaseTimeMs` | `f64` | bidirectional |  |
| 75 | `dexpSideChannelFilterEnabled` | `bool` | bidirectional |  |
| 76 | `antiVoxGainDb` | `i64` | bidirectional |  |
| 77 | `twoToneFreq1` | `i64` | bidirectional |  |
| 78 | `twoToneFreq2` | `i64` | bidirectional |  |
| 79 | `twoToneLevel` | `f64` | bidirectional |  |
| 80 | `twoTonePower` | `i64` | bidirectional |  |
| 81 | `twoTonePulsed` | `bool` | bidirectional |  |
| 82 | `twoToneInvert` | `bool` | bidirectional |  |
| 83 | `twoToneFreq2Delay` | `i64` | bidirectional |  |
| 84 | `twoToneDrivePowerSource` | `enum` | bidirectional | 0, 1, 2 |

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
| `accessoryData` | `AccessoryDataModel` |
| `accessorySettings` | `AccessorySettingsModel` |
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
  minor 11 on a Core that owns its accessories, `accessoryData` only at
  minor 11 while `accessoryDataVersion` is at least 1, `accessorySettings` only at
  minor 11 while `remotePgxlControlVersion` is at least 3 or
  `remoteTgxlControlVersion` at least 1, and `stationTci` only at
  minor 11 on a Core that runs a station TCI server
  (`StationServer::sendToSession`). An older peer never sees their schema
  either.
- **`transmit`.** Every property is `bidirectional` on the wire, but a
  receive-only Core takes only the transmit settings: at
  `transmitSettingsVersion` 1, a write of any property except `mox`,
  `tune`, `voxEnabled` and `twoToneActive`, and only while its radio is
  off the air (section 7.3). The Core also publishes its real transmit
  state as `radio`'s `transmitting` (outbound); a window reads "on the
  air" from it, the mirrored `transmit.tune`, and `pureSignal`'s
  `twoToneOn`. `radio`'s `txInhibited` (bool, outbound) is the Core's TX
  inhibit (its `TxInhibitMonitor`); a window's TX indicator shows it. It
  needs no capability: a window that does not know it ignores it. A window
  clears its copy of `transmitting` and `txInhibited` when the session
  ends.
- **`transmit` at `transmitSettingsVersion` 2.** Each property carries its
  setter's type: `tunePower` (i64, the fixed tune power Setup uses, 0 to
  100 W, 0 to 99 on a Hermes Lite 2), `voxThresholdDb` (i64, -80 to 0 dB),
  `voxHangTimeMs` (i64, 1 to 2000 ms), `monEnabled` (bool), `monitorVolume`
  (f64, 0.0 to 1.0; a window's slider shows it as 0 to 100),
  `txLevelerOn`, `txEqEnabled`, `cfcEnabled`, `cpdrOn` (bool; `cpdrOn` is
  PROC), `cpdrLevelDb` (i64, 0 to 20 dB), `amCarrierLevel` (i64, 0 to 100
  percent), `dexpEnabled` (bool) and `micGainDb` (i64, -50 to 70 dB).
  `tunePowerForTxBand` (i64, outbound) is the tune power for the band the
  Core transmits on (its transmit slice's band, as TUNE reads it), which
  the TX applet's Tune Power slider shows; it follows the transmit slice
  across bands. `tuneDrivePowerSource` (enum: 0 the drive slider, 1 the
  tune slider, 2 the fixed tune power) is where TUNE takes its power from.
  At version 2 both change only through `setTunePowerForTxBand`, and
  `tuneDrivePowerSource` is outbound; at version 5 it is two-way (Setup >
  Transmit > Power's Tune group), and `tunePowerForTxBand` stays outbound. None of these
  keys the radio. The MON output choice (speakers or phones) is not on the
  link: it is each window's own audio routing.
- **`transmit` at `transmitSettingsVersion` 3.** The radio's microphone
  input, each under its setter's name and type: `micBoost` (bool, the
  +20 dB mic boost), `micXlr` (bool, XLR rather than the 3.5 mm jack on a
  radio with both), `micTipRing` (bool, true when the tip is the mic),
  `micBias` (bool), `micPttDisabled` (bool, true when the mic's PTT is
  ignored), `lineIn` (bool, Line In rather than Mic In) and `lineInBoost`
  (f64, the Line In gain, -34.5 to 12.0 dB). The mic source is not among
  them. `activeTxProfile` (utf8, outbound) is the Core's active TX
  profile, and `txProfilesJson` (utf8, outbound) its TX profiles as a JSON
  array of names in the Core's order (for example
  `["AM","Default","Default DX"]`); a station with no radio sends `""` and
  `[]`. Both change only through the `txProfile.*` commands (or at the
  Core). TX and mic profiles are one set. A window never keeps its own
  copy of the Core's profiles: its profile combos and Setup > Audio > TX
  Profile show these two and ask the Core. None of these keys the radio.
- **`transmit` at `transmitSettingsVersion` 4.** The TX EQ, CFC, phase
  rotator, CESSB, leveler and ALC settings, each under its setter's name
  and type. `txEqUseLegacy` (bool, default true) is the TX EQ dialog's
  Legacy EQ box: true, the ten-band EQ reaches the TX channel; false, the
  parametric curve in `txEqParaEqData` does. The Core applies the curve
  itself, whichever window changed it, and it is saved with the TX profile
  (Thetis's `EQUseLegacy`). `txEqPreamp` (i64, -12 to 15 dB),
  `txEqBandsJson` (utf8, the ten band gains, each -12 to 15 dB),
  `txEqFreqsJson` (utf8, the ten band centres, each 10 to 22000 Hz),
  `txEqNc` (i64, 32 to 8192), `txEqMp` (bool), `txEqCtfmode` (i64, 0
  peaking or 1 notch), `txEqWintype` (i64, 0 Blackman-Harris or 1 Hann),
  `txEqParaEqData` (utf8, the parametric curve as Thetis saves it: gzip,
  then base64url, of the curve's JSON; empty for none). `cfcCompressionJson`
  (utf8, the ten compression levels, each 0 to 16 dB), `cfcEqFreqJson`
  (utf8, the ten band centres, each 0 to 20000 Hz),
  `cfcPostEqBandGainJson` (utf8, the ten post-EQ gains, each -24 to 24 dB),
  `cfcPostEqEnabled` (bool), `cfcPostEqGainDb` (i64, -24 to 24 dB),
  `cfcPrecompDb` (i64, 0 to 16 dB), `cfcParaEqData` (utf8, as
  `txEqParaEqData`). A non-empty `txEqParaEqData` or `cfcParaEqData` the
  Core cannot read as a curve is refused ("The Core could not read that
  equalizer curve. Save the curve again and retry.") and changes nothing. `phaseRotatorEnabled` (bool), `phaseRotatorFreqHz`
  (i64, 10 to 2000 Hz), `phaseRotatorStages` (i64, 2 to 16),
  `phaseReverseEnabled` (bool), `cessbOn` (bool), `txLevelerMaxGain` (i64,
  0 to 20 dB), `txLevelerDecay` (i64, 1 to 5000 ms), `txAlcMaxGain` (i64, 0
  to 120 dB) and `txAlcDecay` (i64, 1 to 50 ms). Each ten-value array is a
  compact JSON array of ten whole numbers in band order (for example
  `[-12,-12,-12,-1,1,4,9,12,-10,-10]`); an array of any other length, or
  with a value that is not a whole number or is out of range, is refused
  whole and changes nothing. None of these keys the radio.
- **`transmit` at `transmitSettingsVersion` 5.** Setup > Transmit > Power,
  Transmit > DEXP/VOX and Test > Two-Tone IMD, each under its setter's
  name and its getter's type. `tuneDrivePowerSource` becomes two-way.
  `powerByBandJson` and `tunePowerByBandJson` (utf8) are the per-band power
  and tune power in whole watts, a compact JSON object keyed by the app's
  band key for the 14 bands (`160m`, `80m`, `60m`, `40m`, `30m`, `20m`,
  `17m`, `15m`, `12m`, `10m`, `6m`, `GEN`, `WWV`, `XVTR`); the Core writes
  its keys in its own order, and a window reads it as an object. A write
  carries all 14 bands, each a whole number from 0 to 100 W (tune power 0
  to 99 on a Hermes Lite 2); a map with a band missing, a key that is not
  a band, or a value that is not a whole number or is out of range is
  refused whole and changes nothing. A map the Core takes reads back as
  the same object in the Core's key order, and is accepted.
  `dexpAttackTimeMs` (f64, 2 to 100 ms), `dexpDetectorTauMs` (f64, 1 to 100
  ms), `dexpExpansionRatioDb` (f64, 0.0 to 30.0 dB), `dexpHighCutHz` and
  `dexpLowCutHz` (f64, 100 to 10000 Hz, the VOX trigger filter),
  `dexpHysteresisRatioDb` (f64, 0.0 to 10.0 dB), `dexpLookAheadEnabled`
  (bool), `dexpLookAheadMs` (f64, 10 to 999 ms), `dexpReleaseTimeMs` (f64, 2
  to 1000 ms), `dexpSideChannelFilterEnabled` (bool), `antiVoxGainDb` (i64,
  -60 to 60 dB). `twoToneFreq1` and `twoToneFreq2` (i64, -20000 to 20000
  Hz), `twoToneLevel` (f64, -96 to 0 dB), `twoTonePower` (i64, 0 to 100
  percent), `twoTonePulsed` and `twoToneInvert` (bool), `twoToneFreq2Delay`
  (i64, 0 to 1000 ms) and `twoToneDrivePowerSource` (enum, as
  `tuneDrivePowerSource`). The two-tone settings are read when a two-tone
  test starts; the test itself (`twoToneActive`) and Enable VOX
  (`voxEnabled`) stay in the keying set. None of these keys the radio. The
  Core's runtime SWR foldback (Thetis's `NetworkIO.SWRProtect`) is not a
  setting and is not on the link.
- **`stepAtt` at `transmitSettingsVersion` 5.** Setup > Transmit > Power's
  `attOnTxEnabled` (bool, ATT on TX), `attOnTxValue` (i64, the ATT on TX
  value in dB for the Core's transmit band, from the Core's attenuator
  minimum to 31: 0 to 31, -28 to 31 on a Hermes Lite 2) and
  `forceAttWhenPsOff` (bool, Force ATT on Tx to 31 when PS-A is off),
  declared after `adcLinked`. The Core applies each through its step
  attenuator, as the local page does; `attOnTxValue` also follows
  PureSignal's AutoAtt. They are transmit settings: a receive-only Core
  takes them from a peer offered `transmitSettingsVersion` while its radio
  is off the air (section 7.3). Changing `attOnTxValue` with ATT on TX on
  sets the radio's TX attenuator; it keys nothing.
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
`stationTci`, `accessoryData`, `accessorySettings`), a `stepAtt` or `alexAntennas` write from a peer below minor
11 or while the Core has no controller behind it, a raw write of `radio`'s
`rfKitEnabled` (changed only with `setRfKitEnabled`), the keying
`transmit` properties (`mox`, `tune`, `voxEnabled`, `twoToneActive`) on a
receive-only station, whether or not the station mirrors them ("Transmit
configuration is unavailable on this receive-only Core."), any
`transmit` property on a receive-only station from a peer below agreed
minor 11 (it was never offered `transmitSettingsVersion`; the same
reason), any other `transmit` property on a receive-only station while
its radio is on the air ("The radio is on the air. Try again when it stops.": keyed through
its `MoxController` from any source, a hardware PTT included, until the
hand-back to receive ends; TUNE on; or the two-tone test running), a
`transmit` setting outside its setter's range, with the range ("Choose a
tune power from 0 to 100 W.", "Choose a VOX level from -80 to 0 dB.",
"Choose a VOX delay from 1 to 2000 ms.", "Choose a monitor level from 0.0
to 1.0.", "Choose a PROC level from 0 to 20 dB.", "Choose an AM carrier
level from 0 to 100 percent.", "Choose a mic level from -50 to 70 dB.",
"Choose a Line In gain from -34.5 to 12.0 dB.", "Choose a TX EQ preamp
from -12 to 15 dB.", "Choose a TX EQ Nc from 32 to 8192.", "Choose a TX EQ
cutoff of 0 (peaking) or 1 (notch).", "Choose a TX EQ window of 0
(Blackman-Harris) or 1 (Hann).", "Choose a CFC pre-compression from 0 to 16
dB.", "Choose a CFC post-EQ gain from -24 to 24 dB.", "Choose a phase
rotator frequency from 10 to 2000 Hz.", "Choose from 2 to 16 phase rotator
stages.", "Choose a leveler maximum gain from 0 to 20 dB.", "Choose a
leveler decay from 1 to 5000 ms.", "Choose an ALC maximum gain from 0 to
120 dB.", "Choose an ALC decay from 1 to 50 ms."; a ten-value array of
the wrong length or with a value out of range is refused whole: "Choose
ten TX EQ band levels, each from -12 to 15 dB.", "Choose ten TX EQ band
centres, each from 10 to 22000 Hz.", "Choose ten CFC compression levels,
each from 0 to 16 dB.", "Choose ten CFC band centres, each from 0 to 20000
Hz.", "Choose ten CFC post-EQ band levels, each from -24 to 24 dB.";
at `transmitSettingsVersion` 5, "Choose a DEXP attack time from 2 to 100
ms.", "Choose a DEXP detector time from 1 to 100 ms.", "Choose a DEXP
release time from 2 to 1000 ms.", "Choose a DEXP expansion ratio from 0.0
to 30.0 dB.", "Choose a DEXP hysteresis ratio from 0.0 to 10.0 dB.",
"Choose a look-ahead time from 10 to 999 ms.", "Choose a VOX trigger
filter cut from 100 to 10000 Hz.", "Choose an anti-VOX gain from -60 to 60
dB.", "Choose a tone frequency from -20000 to 20000 Hz.", "Choose a
two-tone level from -96 to 0 dB.", "Choose a two-tone power from 0 to 100
percent.", "Choose a second tone delay from 0 to 1000 ms.", and a band map
refused whole: "Choose a power from 0 to 100 W for each of the 14 bands.",
"Choose a tune power from 0 to 100 W for each of the 14 bands."; a Hermes
Lite 2 says "Choose a tune power from 0 to 99." and "Choose a tune power
from 0 to 99 for each of the 14 bands."), `stepAtt`'s `attOnTxEnabled`,
`attOnTxValue` and `forceAttWhenPsOff` on a receive-only station from a
peer not offered `transmitSettingsVersion` (the receive-only reason) or
while its radio is on the air (the on-air reason), and `attOnTxValue`
outside the Core's range ("Choose an ATT on TX value from 0 to 31 dB.", on
a Hermes Lite 2 from -28), a `pureSignalSettings` write from a peer
offered `transmitSettingsVersion` 7 while the radio is on the air (the
on-air reason; from such a peer an accepted write is applied to the Core's
PureSignal at once, and from any other peer it is kept and applied when
PureSignal next starts, as before), and
DSP settings from a peer that did not negotiate them
(`StationServer::handlePropertyWrite`). The on-air check is read once for
the whole write, before anything in it is applied.
A write to an `outbound` property is refused before anything is applied,
with the reason "The Core sets this itself; it cannot be changed from
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
| 3. whole key | `NetworkWatchdogEnabled` | station |
| 3. whole key | `DisableHfPa` | operatorLocal |
| 3. whole key | `ExtendedTxAllowed` | operatorLocal |
| 3. whole key | `PreventTxOnDifferentBandToRx` | operatorLocal |
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

The station refuses a write to a key outside the station scope ("Each app
keeps this setting itself; the Core does not store it."), to another radio's `hardware/<mac>/` keys ("These settings
are for a radio this Core is not connected to."), an out-of-range
`SwrProtectionLimit` ("Choose an SWR protection limit from 1.0 to 5.0."), and transmit-side keys on a receive-only
station ("Transmit configuration is unavailable on this receive-only
Core."). At `transmitSettingsVersion` 1 a receive-only station takes the
DSP > Options TX keys (`DspOptions<Setting><Mode>Tx`,
`StationServer::isTransmitSettingKeyAcceptedOffAir`) while its radio is off
the air, and refuses a write or remove of one while it is on the air ("The
radio is on the air. Try again when it stops."), handing back its own
value. At `transmitSettingsVersion` 5 the same holds for Setup >
Transmit > Power's SWR Protection keys (`SwrProtectionEnabled`,
`SwrProtectionLimit`, `SwrTuneProtectionEnabled`, `TunePowerSwrIgnore`,
`WindBackPowerSwr`) and External TX Inhibit keys
(`TxInhibitMonitorEnabled`, `TxInhibitMonitorReversed`), on any peer. A
value the page's own control cannot hold is refused with the Core's value
handed back: `TunePowerSwrIgnore` outside 5 to 50 ("Choose a tune power
to ignore from 5 to 50 W."), a box that is not `True` or `False` ("The
Core expected this box to be on or off."). A taken SWR Protection key, or
its removal, applies to the Core's SWR protection at once (a removal
returns the default: off, limit 2.0, tune power to ignore 35 W). A taken
External TX Inhibit key is stored on the Core, whose TX inhibit gate
follows it (the receiver and transmit gaps plan, Task 13). At
`transmitSettingsVersion` 6 the same off-air rule holds for Setup > PA's
keys, `hardware/<mac>/pa/...` (the PA profiles: the profile list
`pa/profile/_names`, each profile `pa/profile/<name>`, and the active
profile `pa/profile/active`) and `hardware/<mac>/paCalibration/...` (the
PA forward-power table, `boardClass` and `calPoint1` to `calPoint10`),
from a peer at agreed minor 11. A taken key, or its removal, applies to
the Core's PA profiles or calibration at once, and a window reloads its
copies of both when those keys change. The rest of the transmit-side
hardware keys stay refused.

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
wording. The generic families give "The Core changes these settings only
through their own controls." for a write, and "Change these settings with
their own controls on this Core." for a remove.

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

Every handler requires exactly the arguments listed, and an extra or
missing argument is refused. The one exception is `setPgxlHardware`, whose
three arguments are marked optional: it takes exactly one of them per
command (`biasMode` "ClassA" or "ClassAB", `fanMode` "Auto", "Quiet" or
"Continuous", or `ledIntensity` 0 to 100), and none or more than one is
refused.

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
| `setPgxlName` | `name` utf8 | `remotePgxlControlVersion` | 3 | 11 |
| `setPgxlHardware` | `biasMode` utf8 (optional), `fanMode` utf8 (optional), `ledIntensity` i64 (optional) | `remotePgxlControlVersion` | 3 | 11 |
| `setPgxlNetwork` | `dhcp` bool, `address` utf8, `netmask` utf8, `gateway` utf8 | `remotePgxlControlVersion` | 3 | 11 |
| `savePgxlSettings` | none | `remotePgxlControlVersion` | 3 | 11 |
| `readPgxlSettings` | none | `remotePgxlControlVersion` | 3 | 11 |
| `setPgxlOperate` | `on` bool | `remotePgxlControlVersion` | 4 | 11 |
| `scanPgxlLan` | none | `remotePgxlControlVersion` | 4 | 11 |
| `setPgxlAddress` | `host` utf8, `port` i64 | `remotePgxlControlVersion` | 4 | 11 |
| `setTgxlName` | `name` utf8 | `remoteTgxlControlVersion` | 1 | 11 |
| `setTgxlNetwork` | `dhcp` bool, `address` utf8, `netmask` utf8, `gateway` utf8 | `remoteTgxlControlVersion` | 1 | 11 |
| `saveTgxlSettings` | none | `remoteTgxlControlVersion` | 1 | 11 |
| `readTgxlSettings` | none | `remoteTgxlControlVersion` | 1 | 11 |
| `setTgxlAntenna` | `port` i64 | `remoteTgxlControlVersion` | 2 | 11 |
| `setTgxlOperate` | `on` bool | `remoteTgxlControlVersion` | 2 | 11 |
| `setTgxlBypass` | `on` bool | `remoteTgxlControlVersion` | 2 | 11 |
| `moveTgxlRelay` | `relay` i64, `direction` i64 | `remoteTgxlControlVersion` | 4 | 11 |
| `scanTgxlLan` | none | `remoteTgxlControlVersion` | 4 | 11 |
| `setTgxlAddress` | `host` utf8, `port` i64 | `remoteTgxlControlVersion` | 4 | 11 |
| `setTunePowerForTxBand` | `watts` i64 | `transmitSettingsVersion` | 2 | 11 |
| `txProfile.select` | `name` utf8 | `transmitSettingsVersion` | 3 | 11 |
| `txProfile.save` | `name` utf8 | `transmitSettingsVersion` | 3 | 11 |
| `txProfile.delete` | `name` utf8 | `transmitSettingsVersion` | 3 | 11 |
| `rade.resetVocoder` | none | `transmitSettingsVersion` | 3 | 11 |
| `configureRfKit` | `host` utf8, `port` i64 | `remoteRfKitControlVersion` | 2 | 11 |
| `disconnectRfKit` | none | `remoteRfKitControlVersion` | 2 | 11 |
| `setRfKitEnabled` | `enabled` bool | `remoteRfKitControlVersion` | 2 | 11 |
| `resetRfKitError` | none | `remoteRfKitControlVersion` | 3 | 11 |
| `setRfKitOperate` | `on` bool | `remoteRfKitControlVersion` | 4 | 11 |
| `setRfKitAntenna` | `port` i64 | `remoteRfKitControlVersion` | 4 | 11 |
| `setRfKitTciMode` | none | `remoteRfKitControlVersion` | 4 | 11 |
| `setRfKitAddress` | `host` utf8, `port` i64 | `remoteRfKitControlVersion` | 4 | 11 |
| `setStationTci` | `enabled` bool, `port` i64 | `stationTciVersion` | 1 | 11 |
| `setTxInterlockPolicy` | `mode` i64, `graceMs` i64, `swrGateEnabled` bool, `swrGateMax` f64 | `accessoryDataVersion` | 1 | 11 |
| `setPgxlPowerCap` | `enabled` bool, `watts` i64 | `accessoryDataVersion` | 1 | 11 |
| `clearAccessoryFaults` | `device` utf8 | `accessoryDataVersion` | 1 | 11 |
| `requestIoBoardProbe` | none | `radioHardwareVersion` | 2 | 11 |
| `setAlexRxAntenna` | `band` i64, `antenna` i64, `rxOnly` bool | `radioHardwareVersion` | 3 | 11 |
| `setAlexBpfMode` | `chain` i64, `mode` i64 | `radioHardwareVersion` | 4 | 11 |
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

Ten command groups need a sentence beyond the table:

- **The filter policy.** `setAlexBpfMode` sets one receive filter chain's
  filter policy (`chain` 0 or 1; `mode` 0 Auto, 1 Force filter, 2 Force
  bypass), the call the Core's own filter policy dialog makes on Apply.
  The Core saves it for its radio and publishes each chain's state on the
  `radio` object (`rxFilter0Mode` to `rxFilter1Reason`) in a `delta`. The
  policy picks the receive band-pass filter only, so a receive-only Core
  applies it too.
- **The amp's and tuner's own settings.** `setPgxlName`,
  `setPgxlHardware`, `setPgxlNetwork`, `savePgxlSettings` and
  `readPgxlSettings`, and `setTgxlName`, `setTgxlNetwork`,
  `saveTgxlSettings` and `readTgxlSettings`, change or read the Power
  Genius's and the Tuner Genius's own settings, the ones their Advanced
  pages show. `accepted` means the Core sent the request to the device; the
  device's answer arrives later on the read-only `accessorySettings`
  object (`pgxlAnswer`, `pgxlAnswerAccepted` and `pgxlAnswerCount`, and
  the `tgxl` trio), with the settings the device last reported. A save
  restarts the device. None of them keys a transmitter.
- **The tuner's antenna, operate and bypass.** `setTgxlAntenna` (`port`
  1, 2 or 3), `setTgxlOperate` and `setTgxlBypass` (`on`) switch the
  Core's Tuner Genius through its own tuner model, sending the tuner the
  line a local window's Tuner Genius applet sends. `accepted` means the
  command left for the tuner; the tuner's report arrives on the `tuner`
  object (`antennaA`, `isOperate`, `isBypass`). They key nothing, so a
  receive-only Core takes them, but each is refused while the radio is on
  the air (MOX, TUNE or two-tone, or the hand-back to receive after MOX),
  with no tuner admitted, and for the
  antenna on a tuner with no antenna switch or a port outside 1 to 3.
  The reasons are in
  [remote accessory control version 1](2026-09-23-remote-accessory-control-v1.md).
- **The tuner's relays, Scan LAN and saved address.** `moveTgxlRelay`
  (`relay` 0 C1, 1 L, 2 C2; `direction` -1 or 1) moves one matching relay
  one step through the Core's tuner model, sending the line a local
  window's relay bar sends on a mouse-wheel step; the tuner's report
  arrives on `tuner` (`relayC1`, `relayL`, `relayC2`). `scanTgxlLan` has
  the Core listen for Tuner Genius announcements for three seconds; its
  `command.result` is sent when the window ends, with `values` holding
  `devicesJson` (utf8, a JSON array of
  `{"address","port","model","serial","nickname"}`). `setTgxlAddress`
  (`host`, `port`) saves the tuner's address for the Core's radio without
  dialling, with `configureTgxl`'s checks. None keys anything, so a
  receive-only Core takes them, but each is refused while the radio is
  on the air ("The radio is on the air. Try again when it stops."); the
  nudge also with no tuner admitted. The reasons are in the remote
  accessory control document.
- **The amp's OPERATE, Scan LAN and saved address.** `setPgxlOperate`
  (`on` bool) sends the Core's Power Genius the line a local window's
  applet OPERATE sends, `operate=1` or `operate=0`, through the Core's own
  connection; `accepted` means the line left, and the amp's report arrives
  on `amplifier` (`state`, `deviceState`, `operate`). `scanPgxlLan` and
  `setPgxlAddress` (`host`, `port`) are `scanTgxlLan` and
  `setTgxlAddress` for the Power Genius: Power Genius announcements only,
  `values` `devicesJson` when the three seconds end, and `PGXL_ManualIp`
  and `PGXL_ManualPort` saved without dialling with `configurePgxl`'s
  checks. None keys anything, so a receive-only Core takes them, but each
  is refused while the radio is on the air ("The radio is on the air. Try
  again when it stops."); `setPgxlOperate` also while the Core is not
  connected to the amp. A `property.write` of `amplifier` `operate` stays
  refused. The reasons are in the remote accessory control document.
- **The RF-Kit's OPERATE, antenna, TCI mode and saved address.**
  `setRfKitOperate` (`on` bool), `setRfKitAntenna` (`port` i64, internal
  antenna 1 to 4) and `setRfKitTciMode` (no arguments) send the Core's
  admitted RF2K-S the REST request a local window's applet OPERATE, ANT
  button and RF-Kit page's "Set amp to TCI mode" send (`PUT /operate-mode`,
  `PUT /antennas/active`, `PUT /operational-interface`); `accepted` means
  the request left, and the amp's report arrives on `rfkit` (`operate`,
  `activeAntennaNumber`, `operationalInterface`). An antenna the amp lists
  as disabled, or does not list once it has listed its antennas, is
  refused. `setRfKitAddress` (`host`, `port`) saves `RfKit_ManualIp` and
  `RfKit_ManualPort` without dialling with `configureRfKit`'s address
  checks. None keys anything, so a receive-only Core takes them, but each
  is refused while the radio is on the air ("The radio is on the air. Try
  again when it stops."); the first three also while the Core is not
  connected to the amp. The reasons are in the remote accessory control
  document.
- **The Tune Power slider.** `setTunePowerForTxBand` (`watts`, 0 to 100,
  0 to 99 on a Hermes Lite 2) does what the TX applet's Tune Power slider
  does in a local window: it sets the tune power for the band the Core
  transmits on and sets the tune drive source to the tune slider, so TUNE
  uses that power. The Core reports both on `transmit`
  (`tunePowerForTxBand`, `tuneDrivePowerSource`). It keys nothing, so a
  receive-only Core takes it, but it is refused while the radio is on the
  air ("The radio is on the air. Try again when it stops."), outside the
  tune power range ("Choose a tune power from 0 to 100 W.", or "Choose a
  tune power from 0 to 99." on a Hermes Lite 2), and when not understood
  ("The request to change the tune power was not understood."). A peer
  below agreed minor 11 gets "Update this app to change the tune power on
  this Core."
- **TX profiles and the RADE vocoder.** `txProfile.select` (`name`)
  applies the named TX profile on the Core as the TX applet's profile
  combo does in a local window: its settings become the Core's transmit
  settings and it becomes the active profile. `txProfile.save` (`name`)
  saves the Core's current transmit settings under the name, as Setup >
  Audio > TX Profile's Save... does: a new name adds a profile, the name
  of an existing one overwrites that profile only, and a comma in the name
  becomes `_`. `txProfile.delete` (`name`) deletes the profile; deleting
  the active one makes another active, as locally. The Core reports its
  active profile and list on `transmit` (`activeTxProfile`,
  `txProfilesJson`). `rade.resetVocoder` (no arguments) clears the RADE
  transmit vocoder of the Core's active slice, as the RADE applet's Reset
  vocoder does. None keys the radio, so a receive-only Core takes them,
  but each is refused while the radio is on the air ("The radio is on the
  air. Try again when it stops."). The other refusals: "There is no
  transmit profile called <name>." (select and delete), "Give the transmit
  profile a name." (save with a blank name), "The Core has no radio to
  keep transmit profiles for." (save before the Core has a radio), "It is
  not possible to delete the last remaining TX profile." (the local page's
  own words), "RADE is not running on the Core's active slice." (reset
  with no RADE channel), "The request for the transmit profile was not
  understood." and "The request to reset the RADE vocoder was not
  understood." A peer below agreed minor 11 gets "Update this app to
  change transmit profiles on this Core."
- **PureSignal arming.** `ps3.single` (Single Cal), `ps3.automatic`
  (Automatic, and PS-A on), `ps3.applyCurrent` (Apply current correction)
  and `ps3.restoreCorrection` (Restore a saved correction) arm PureSignal
  on the Core as a local window's PureSignal controls do. Arming keys
  nothing: it sets the calibration engine's state, and the correction runs
  only while the radio transmits. A Core at `transmitSettingsVersion` 7
  takes them from a peer at agreed minor 11 while its radio is off the air
  and refuses them while it is on the air ("The radio is on the air. Try
  again when it stops."). Any other peer gets "PureSignal cannot be run
  from a remote window yet." as before, and so does `ps3.twoTone` with
  `enabled` true from every peer: the two-tone test keys the radio and
  waits for remote transmit. `ps3.off`, `ps3.twoTone` with `enabled` false
  and `ps3.saveCorrection` are taken as before. A taken action answers
  `accepted`, then `completed` or `failed` with the Core's reason (section
  9.1, the `phase` value), and the Core's `pureSignal` and
  `pureSignalSettings` objects follow in a `delta`.
- **One TCI switch.** The Core keeps one TCI switch and port for its own
  TCI server (`setStationTci`, the `stationTci` object). A desktop window
  connected to a Core with `stationTciVersion` 1 shows that switch and
  port as its own and changes them with `setStationTci`; an app does the
  same. The Core keeps the switch when the client leaves, so its server
  keeps running for the RF-Kit amplifier and for other apps. The wire is
  unchanged from `stationTciVersion` 1.

### 9.2 Unknown verbs

A verb the station does not route gets `command.result` with `accepted`
false and the reason "The Core does not know this request. Updating the
Core may help." (or "The Core does not know this PureSignal action." and
"The Core does not know this request." inside those families). The
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
version at least 3, and the radio section's PA readings and link quality
unless the minor is at least 11 and the version at least 4. A message
over 16 KiB is refused.

At version 4 (remote-window parity Task 6) the radio section carries, each
absent when the radio has none or has not reported it and all absent while
the radio is not connected: `paVolts` (the PA drain volts, user ADC0),
`supplyVolts`, `paCurrentAmps` and `paTemperatureCelsius` (the Core's
`RadioModel::paReadings()`), `packetLossPercent` (lost over received plus
lost in the last 5 seconds, from the sequence errors Thetis counts, one
per mismatch), `jitterMs` (RFC 3550 section 6.4.1 interarrival jitter of
the lowest active receive stream), `packetGapMs` (the longest interval
between two datagrams from the radio in the last second), `sampleRateHz`
and `udpPacketsSeen` (datagrams from the radio since it connected). Volts,
amps, jitter and gap are finite and not negative, the loss is 0 to 100, a
temperature is not below absolute zero, and the counts are whole numbers.
A window shows each as the Core's, and one that is absent or out of date
as unavailable, never 0.

<!-- surface:telemetry -->
<!-- Generated by scripts/render-link-tables.py from tests/data/link/v1/surface.json. Do not edit by hand. -->

Message kind `station.metrics.v1`.

| `stationTelemetryVersion` | Minimum minor | Field paths | Field paths added |
| --- | --- | --- | --- |
| 1 | 3 | 15 | `audio.active`, `audio.contextGeneration`, `audio.encodeFailuresPerSecond`, `audio.encodedPacketsPerSecond`, `audio.sendAcceptedPerSecond`, `audio.sendRejectedPerSecond`, `audio.sourceDropsPerSecond`, `audio.sourceFramesPerSecond`, `radio.connected`, `radio.rttAgeMs`, `radio.rttMs`, `radio.rxMbps`, `radio.txMbps`, `sampledElapsedMs`, `sequence` |
| 2 | 10 | 22 | `host.hottestZoneCelsius`, `host.hottestZoneName`, `host.memoryAvailableKiB`, `host.memoryTotalKiB`, `host.processCpuPercent`, `host.processResidentKiB`, `host.systemCpuPercent` |
| 3 | 11 | 26 | `receivers[].inputDelayMs`, `receivers[].loadPercent`, `receivers[].skippedInputMs`, `receivers[].sliceId` |
| 4 | 11 | 35 | `radio.jitterMs`, `radio.paCurrentAmps`, `radio.paTemperatureCelsius`, `radio.paVolts`, `radio.packetGapMs`, `radio.packetLossPercent`, `radio.sampleRateHz`, `radio.supplyVolts`, `radio.udpPacketsSeen` |

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
the link dead and ends it with "This app stopped answering, so the Core
closed the connection.", `retryable` true. The
round-trip time of a pong is recorded for diagnostics only; a slow pong is
never a missed one.

### 12.2 The connect deadline

The whole connect sequence must finish within 30 s
(`kStationHandshakeDeadlineMs` 30000) of the WebSocket opening. The
station ends a connection that has not reached `snapshot.complete` by then
with "This app did not finish connecting to the Core in time.", `retryable`
true. The desktop client
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
  gets `session.end` "The Core already has as many connections as it
  allows. Try again shortly.",
  `retryable` true, because a reconnecting client meets it while its own
  dead sockets drain.

### 12.4 Ending, preemption and retryable

`session.end` carries a `reason` and `retryable`. `auth.result` carries
`retryable` too. A client redials only after a retryable end; after one
that is not retryable it stops and tells the operator.

| Cause | Message | `retryable` |
| --- | --- | --- |
| Wrong token | `auth.result` accepted false, "The Core did not accept this app's pairing token. Check the token saved for this Core.", then the station closes | false |
| Token checks locked out (section 3.3) | `auth.result` accepted false, "The Core is refusing pairing tokens for a while after too many wrong ones. Try again later." | true |
| No shared major (section 6.1) | `session.end` naming both sides' versions and the side to update | false |
| Message the station cannot decode (section 13) | `session.end` "The Core could not read a message from this app." | false |
| Out-of-order handshake (section 5.1) | `session.end` | false |
| Preempted by a newer authenticated connection | `session.end` "Another app at ... connected to the Core and took over. Connect again to take it back." | false |
| Connection limit reached | `session.end` | true |
| Connect deadline expired | `session.end` | true |
| Heartbeat timeout | `session.end` | true |
| Station shutting down | `session.end` "The Core is shutting down." | true |

The takeover and version reasons are worded in one place,
`src/core/session/SessionEndReasons.{h,cpp}`: "Another app at
*address:port* connected to the Core and took over. Connect again to take
it back." and "This Core runs link version *N* and this app runs version
*M*. Update the Core." (or "Update this app." when the app is the older
side). An app that offers its own next steps for these two (take the Core
back, check for updates) reads them by these exact words; the desktop
client does (`SessionEndReasons::parse`). This is interim: a later part of
the iPhone plan (Part C) adds an end code to `session.end` that replaces
reading the words.

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
  `media.control`. It sends `session.end` "The Core could not read a
  message from this app." with
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
  `{}` for a fixture every major-1 station passes. Each version is a
  minimum: a station passes the fixture's requirement when it advertises
  that capability at the version named **or later**. A verb's own gate
  can be stricter, and the fixture then holds a station to that gate too:
  `session-verbs-ps3` requires `psAlgorithmVersion` 3, and the PureSignal
  action verbs it invokes need `psAlgorithmVersion` **equal to** 3
  (section 6.2), so a station advertising 4 meets the requirement but not
  those verbs' gate. `linkMajors` is the
  link majors the suite covers, whole numbers from 1 to 65535, oldest
  first, without repeats; the station's runners check each against the
  majors the station supports (`LinkVersion::supportedMajors()`). Each runner runs its fixtures once per
  major in it, against a station that offers that major, and fails when
  this station does not offer it. Every file under
  `control/`, `sessions/` and `media/` is listed once; a media entry names
  its `.bin`, and its `.expect.json` sits beside it.
- `control/*.json`: `{"from":"station"|"client","wire":{<the exact message>},"decodes":true|false}`.
- `sessions/*.json`:
  `{"runs":[<ends>],"stationSetup":{<the fake radio model and settings>},"steps":[<steps>]}`.
  `runs` names the ends that run the fixture, each once: `"station"` (the
  station's runner, which runs every fixture) and `"app"` (an app's
  runner, section 16.3). A step is one of
  `{"from":"station","message":{<a message>}}`,
  `{"from":"client","role":"behaviour"|"scripted","message":{<a message>}}`,
  `{"advanceMs":N}` or `{"expectClosed":{"retryable":true|false}}`.
  No other key is allowed at either level.
- `media/*.bin` with `*.expect.json`: the bytes of one packet exactly as
  it travels, and `{"codec":<codec>,"expect":{<decoded values>}}`, where
  `<codec>` is `nsdc1`, `ps3d`, `opus` or `nrsc1` (the LAN announcement of
  section 14). `expect` may hold `"after": ["<fixture id>", ...]` for a
  codec whose decoder keeps state (section 16.4).

A message in a fixture may hold placeholders in place of a value. Each is
a JSON string:

| Placeholder | Matches | Station message | Client message |
| --- | --- | --- | --- |
| `"$any"` | any value; the key must be present | yes | no |
| `"$string"` | any string | yes | yes, filled with `"conformance"` |
| `"$string:<name>"` | any string, recorded under `<name>` | yes, but not in a fixture for the app | yes, filled with `"conformance"` and recorded |
| `"$int"` | any whole number | yes | yes, filled with `0` |
| `"$int:<name>"` | any whole number, recorded under `<name>` | yes, but not in a fixture for the app | yes, filled with the next number of the fixture's counter (1 first, then 2, ...) and recorded |
| `"$int:<name>:<min>:<max>"` | a whole number from `<min>` to `<max>` (each a whole number in JSON syntax), recorded under `<name>` | yes, but not in a fixture for the app | yes, filled as `"$int:<name>"`; a counter value outside the range is a malformed fixture |
| `"$object"` | any JSON object | yes, but not in a fixture for the app | yes, filled with `{}` |
| `"$majors"` | only as the `majors` of a `hello`: a non-empty array of whole numbers from 0 to 65535, ascending, without repeats, that holds the same message's `major` | yes, but not in a fixture for the app | yes, filled with `[major]`, the message's own (filled) `major` alone |
| `"$capture:<name>"` | any value, recorded under `<name>` | yes | no |
| `"$ref:<name>"` | the value recorded under `<name>`, compared the same way | yes | yes, filled with the recorded value |
| `"$within:<t>:<v>"` | a number no further than `<t>` from `<v>`; `<t>` and `<v>` are each exactly a JSON number (RFC 8259 section 6: no `+`, no leading `.`, no `inf` or `nan`, no spaces), `<t>` at least 0; any other text is a malformed fixture | yes | no |

A number the station's DSP measures is written `"$within:<t>:<v>"`, with
the tolerance stated, never `"$any"`; a counter whose value depends on
timing is `"$int"`, not pinned.

Matching is by value: objects must have the same keys and arrays the same
length; numbers compare by value, so `1` and `1.0` are equal. A name is
recorded once per fixture run; a later placeholder with the same name
records it again. `token` is recorded before the first step (section
16.3). Where a message is sent rather than matched, "filled" is the value
the sender puts in the placeholder's place.

**Who fills what.** Only the station's runner fills a client message's
placeholders: it sends every client step itself. Its counter starts at 1
in each fixture and advances once for each `"$int:<name>"` it fills, in
step order. An app's runner fills no client placeholder: in a behaviour
step it records what its client chose under each name, and in a fixture
for the app a scripted step holds no placeholder except `"$ref:token"`
(checked), so it has nothing to fill. Each runner therefore records every
name the fixture uses, the station's runner from its counter and fills,
an app's runner from its client; the values can differ between the two
runs, and the station messages that follow refer to them only through
`"$ref:<name>"`, which each runner resolves from its own record.

### 16.2 Control fixtures

Each end decodes `wire`; when `decodes` is true it encodes the result
again and the two JSON objects compare equal after parsing, key order
ignored. The fixtures cover every message kind in each direction it
travels: seven from the client (`hello`, `auth.request`, `command.invoke`,
`media.control`, `property.write`, `settings.write`, `settings.remove`)
and sixteen from the station, with a `delta` carrying `"nan"` and `"-inf"`
(section 4.2). The client's `hello` has two fixtures: an older app's,
without `majors` or `features`, and one declaring both. The station's
`hello` carries both, from a station supporting `[1, 2]`, so `major` is 1,
the oldest (section 6.1). The refusals are: a
`media.control` over its 128 KiB cap and a `station.metrics.v1` over its
16 KiB cap (each an otherwise valid message padded past the cap), a
missing required key (`command.invoke` without `id`), a wrong type (an
`f64` entry holding `true`), an `f64` string other than the three of
section 4.2, a `hello` major above 65535, a `hello` with an empty `majors`,
a `hello` declaring a feature version that is not a whole number, and an
unknown `type`.

The two transport caps (1 MiB into the station, 8 MiB into the desktop
client, section 12.3) are enforced by the WebSocket layer before any
message is decoded, so a decoder fixture cannot hold them; `surface.json`
records them under `limits`.

### 16.3 Session fixtures

A session fixture is a script of one connection between a station and a
client. Two runners play it, one from each end.

- **The station's runner** builds the station `stationSetup` describes,
  opens a client connection to it over an in-process transport, and walks
  the steps. It sends each client message, placeholders filled (section
  16.1), whatever its role, and matches each station message against the
  next one the station sent, in arrival order. It runs every fixture.
- **An app's runner** runs the fixtures whose `runs` names `"app"`. It
  plays the station: it gives its client each station message, filled as
  below, and holds the client to the client steps by role. It ignores
  `stationSetup`, which only says how the station's runner builds its
  station.

**Roles.** Every client step has a `role`:

- `"behaviour"`: the client under test must produce this message. An
  app's runner drives its client to it (connect, invoke this verb with
  these arguments, write this property) and matches what the client sends
  against the step, placeholders allowed; the placeholders' names record
  what the client chose (its ids, its `origin`), and the station messages
  that follow refer to them with `$ref`. The client must send each
  behaviour message in order and nothing else while the steps run; a
  runner may set its client up to do nothing on its own beyond the
  connect sequence.
  A behaviour step's arguments and property values are literals, never
  placeholders: they are what the runner tells its client to send.
- **What the runner drives.** An app's runner drives its client's link
  layer (the part that sends and receives link messages), not its screens.
  That layer must send what it is told whatever the mirrored properties
  say: the summarised stand-ins (below) give, for example, a `pureSignal`
  object with `canActuate` false, and a link layer that refused to send a
  PureSignal verb on that account would fail behaviour steps a real
  station would answer. Deciding what to offer the operator from those
  properties belongs above the link layer.
- `"scripted"`: a message a conformant client never sends (an unknown
  kind or verb, a verb with an argument renamed, a write to a property the
  station sets itself or to one that does not exist, a write without a
  `writeId`, an operator-local setting, an older app's `hello`). An app's
  runner does not wait for its client and sends nothing for the step; it
  goes on, and the station's answers that follow are given to the client
  as if the client had sent the message. They hold the client to handling
  an answer to something it did not send. In a fixture for the app a
  scripted message holds no placeholder but `"$ref:token"`, and its
  `id` or `writeId` is a literal from 1000 up; an app's runner keeps its
  client's own ids below 1000 while a fixture runs, so an answer to a
  scripted message never carries an id the client used.

**Which fixtures run on the app.** A fixture whose client behaviour no
app can adopt runs on the station only (`"runs": ["station"]`): an older
app's `hello` (`major-refused`, `lower-minor`), made-up majors or features
(`version-*`), a client that answers no ping (`heartbeat-missed`) or never
sends its token (`connect-deadline`), and the lockout and preemption,
which need other clients (`lockout`, `preempted`). Their client steps are
all `scripted`. `tst_link_conformance_session` checks that a fixture
marked for the app holds nothing a conformant client could not send, and
nothing an app's runner could not send its client:

- its `hello` and `auth.request` are behaviour: the `hello` has `majors`
  `"$majors"` (an app supporting its own major and the one before sends
  both, section 6.1) and `features` `"$object"`, with `peer`
  `"$string"` and `settingsSchema` `"$int"`; the token is `"$ref:token"`
  or, for a refused token, `"$string"`;
- a behaviour `command.invoke` has `id` `"$int:<name>:1:4294967295"` for
  the `nnr.*`, `ps3.*` and `dspAssets.*` verbs and
  `"$int:<name>:0:4294967295"` for the rest (section 9.1), a verb from
  `commands` with exactly its arguments, names and kinds in order, all
  literal, and its gating capability advertised in the last
  `capabilities` message before it at the version `commands` gives (at
  least that version; exactly it for `psAlgorithmVersion`, section 6.2),
  at an agreed minor of at least the verb's `minMinor`;
- a behaviour `property.write` has `writeId` `"$int:<name>:1:4294967295"`
  (a `writeId` is never 0 on the wire) and writes
  only properties of the object's class (from `mirrorClasses`, the class
  named by the key's `object.create`) that are not outbound, with their
  ordinals and kinds;
- a behaviour `settings.write` or `settings.remove` names a station-scoped
  key (section 8), and a write's `origin` is `"$string:<name>"`;
- a behaviour message is of a kind a client sends (section 16.2);
- a scripted message holds no placeholder but `"$ref:token"`, and a
  scripted `id` or `writeId` is a literal from 1000 up;
- a station message holds no `"$capture:<name>"`, `"$string:<name>"`,
  `"$int:<name>"` (ranged or not), `"$object"` or `"$majors"`, and
  `"$any"` only as a summarised snapshot (below); what an app's runner
  sends for each placeholder a station message may hold is defined
  below.

**Filling a station message (an app's runner).** An app's runner sends
its client each station message with `"$string"` as `""`, `"$int"` as `0`,
`"$ref:<name>"` as the recorded value and `"$within:<t>:<v>"` as `<v>`.

- **The token.** `"$ref:token"` in a client message is the station's
  token. The station's runner reads it from the station at run time; an
  app's runner gives its client a token of its own choosing and records it
  as `token` before the first step. No fixture holds a token.
- **Time.** Time moves only through `{"advanceMs":N}`. The station's
  runner keeps a virtual clock over the station's own timers (the connect
  deadline, the heartbeat and the 50 ms delta flush) and fires each when
  its virtual time comes. A `delta` that waits for the flush follows an
  `{"advanceMs":50}` step. An app's runner moves its client's own clock by
  the same amount, if the client keeps one. No runner sleeps.
- **Closing.** `{"expectClosed":{"retryable":R}}` holds, on the station,
  when the station has closed the connection, every message it sent before
  closing is listed in the steps, and `R` equals `retryable` of the last
  `session.end` or `auth.result` it sent. An authentication refusal sends
  `auth.result` and closes with no `session.end` (section 12.4). An app's
  runner closes the connection there (close code 1000); the client must
  send nothing after it, and must not reconnect on its own when `R` is
  false.
- **The end.** A fixture that does not end in `expectClosed` ends with the
  connection open. Messages the station sends after the last step are not
  checked: on `connect-connectable` the radio keeps sending deltas.
- **The heartbeat.** Pings and pongs are WebSocket frames, not messages
  (section 12.1), so no step holds one. The in-process transport answers a
  ping the way a WebSocket stack does. `heartbeat-answered` passes three
  heartbeat intervals with the link up; `heartbeat-missed` has a client
  that never answers, and the station ends the session on the third
  interval, `retryable` true (section 12.1).
- **Summarised snapshots.** Outside `connect-connectable` (and where a
  fixture needs a value from it, as `verbs-nnr` needs `dspAssets`'
  revision), a `schema` message's `fields` and an `object.create`
  message's `properties` are `"$any"`: `connect-connectable` and
  `surface.json`'s `mirrorClasses` hold their content. An app's runner
  sends, in their place, the class's properties from `mirrorClasses`: for
  `fields`, each as `{"ordinal","name","kind"}`; for `properties`, each as
  `{"kind","name","ordinal","value"}` with this stand-in value by kind:
  `bool` `false`, `i64` `0`, `f64` `0`, `utf8` `""`, and `enum` the first
  of its `enumValues`. The client must accept the stand-ins; the steps that
  follow do not depend on them.

`stationSetup` holds:

| Key | Meaning | Default |
| --- | --- | --- |
| `radio` | `"static"`: a model reporting a connected Hermes Lite 2 (MAC `AA:BB:CC:DD:EE:01`) with no radio behind it, so nothing changes on its own; `"connectable"`: a model connected to the fake Protocol 1 radio, receive processing and all; a receive-only station permits PureSignal (`transmitSettingsVersion` 7), so the runner waits for PureSignal's readiness (`canActuate`) to come on, and it waits for each slice's signal readings to leave the no-reading value before the client attaches | `"static"` |
| `slices` | slices before the client connects | 1 |
| `panadapters` | panadapters before the client connects | 0 |
| `coreAccessories` | the Core owns its accessories: the `tuner`, `amplifier`, `rfkit`, `accessoryData` and `accessorySettings` objects and the accessory commands | false |
| `stationTci` | the Core runs a station TCI server (it stays off) | false |
| `stepAttenuator` | a step attenuator controller is bound, so the radio hardware objects are offered | false |
| `media` | media is enabled | false |
| `priorFailedAuthentications` | other clients that each sent a wrong token before this one connects | 0 |
| `clientAnswersPings` | the client's transport answers the station's pings | true |
| `preemptingClient` | `{"afterStep": i}`: a second client authenticates once step `i` is done | none |
| `otherConnections` | other clients connected before this one, still connecting and sending nothing; 8 puts the station at its connection limit | 0 |

The station runner starts every fixture from an empty settings profile,
and the bundled NR3 model files count as absent, so a fixture reads the
same on every machine.

| Fixture | What it holds the station to |
| --- | --- |
| `connect-connectable` | The whole connect sequence to `snapshot.complete` on a connected radio with one slice, every message in full, except: PureSignal's `statusJson` (`"$string"`, it carries a capture time) and `displayGeneration` (`"$int"`, a counter whose value depends on timing), and the slice's `signalStrengthDbm`, `signalPeakDbm` and `signalAverageDbm`, which the receiver measures: `"$within:0.5:-399.02"`, within 0.5 dB, which leaves out the meter's no-reading value of -400 |
| `wrong-token` | `auth.result` refused, `retryable` false, then the close |
| `connection-limit` | With eight other connections still connecting, the station sends no `hello`: `session.end` "The Core already has as many connections as it allows. Try again shortly.", `retryable` true, then the close |
| `lockout` | After five wrong tokens from other clients, the right token is refused as rate limited, `retryable` true |
| `major-refused` | An older app's `hello` (no `majors`) with major 2 gets `session.end` "This Core runs link version 1 and this app runs version 2. Update the Core.", `retryable` false |
| `version-declares` | A `hello` with `majors` `[1]` and a declared feature is accepted, and authentication follows |
| `version-app-one-ahead` | An app supporting `[1, 2]` chooses 1, the highest it shares with the station, and is accepted |
| `version-app-two-ahead` | An app supporting `[2, 3]` that sends major 3 gets `session.end` "This Core runs link version 1 and this app runs version 3. Update the Core.", `retryable` false |
| `lower-minor` | A `hello` with minor 4 agrees minor 4: the capabilities without the minor-11 entries, and a minor-11 verb refused with a plain reason |
| `preempted` | A second authenticated client ends this session: `session.end`, `retryable` false |
| `heartbeat-answered`, `heartbeat-missed` | The heartbeat, above |
| `connect-deadline` | No `auth.request` within 30000 ms: `session.end` "This app did not finish connecting to the Core in time.", `retryable` true |
| `property-write` | A write and its `property.result` and side-effect `delta`; a refused outbound property and an unknown one; a write without a `writeId` answered by `delta`; a write to a slice's signal strength refused as outbound; on the receive-only Core, a `transmit` write of `power` taken off the air and a write of `mox` and `voxEnabled` refused with the receive-only reason; at `transmitSettingsVersion` 2, a write of `cpdrLevelDb` taken, and `micGainDb` and `monitorVolume` out of range refused with their ranges beside a write of the outbound `tunePowerForTxBand`; at `transmitSettingsVersion` 3, a write of `micBoost` and `lineInBoost` taken, and `lineInBoost` out of range refused with its range beside a write of the outbound `activeTxProfile`; at `transmitSettingsVersion` 4, a write of `txEqBandsJson`, `txEqUseLegacy` and `txLevelerDecay` taken, and a nine-value `txEqBandsJson`, a `cfcCompressionJson` with a value out of range and `txAlcDecay` out of range each refused whole with its range |
| `settings-write` | A station-scoped write echoed with its origin; an operator-local write rejected; a removal sent as `settings.value` with no entry; on the receive-only Core, a DSP > Options TX key and a PA forward-power table key (`paCalibration/calPoint1`, version 6) taken off the air and a transmit hardware key refused |
| `unknown-verb` | `command.result` refused, "The Core does not know this request. Updating the Core may help."; the connection stays up |
| `unknown-kind` | `session.end` "The Core could not read a message from this app.", `retryable` false |
| `verbs-*` | Each verb in `commands`, grouped by the capability that gates it, invoked with its own arguments (for `setPgxlHardware`, one of its three optional ones) and, where it takes any, with one argument renamed (and nothing else changed: the same values and kinds); the two get different answers, so each shows the station read the arguments (a PureSignal action with arguments it does not take is refused "The Core could not read this PureSignal request." before the transmit gate is asked); `verbs-ps3` (which requires `psAlgorithmVersion` 3) invokes the PureSignal verbs whose answers do not depend on arming (`ps3.off`, `ps3.twoTone` off and `ps3.saveCorrection`), and `verbs-ps3-arming` (which also requires `transmitSettingsVersion` 7) invokes `ps3.twoTone` with `enabled` true, refused "PureSignal cannot be run from a remote window yet.", and the arming verbs (`ps3.single`, `ps3.automatic`, `ps3.applyCurrent`, `ps3.restoreCorrection`), taken and then failing on the static station, which has no PureSignal running ("PureSignal is unavailable until the radio is ready."); `nnr.applyModelSelection` names the revision `dspAssets` gave in the snapshot; `verbs-tgxl-control` (which requires `remoteTgxlControlVersion` 2) invokes the antenna, operate and bypass switches, and `verbs-tgxl-relays` (which requires `remoteTgxlControlVersion` 4) matches `scanTgxlLan`'s `devicesJson` as any text, since a real Tuner Genius on the test computer's network may answer, and its accepted `setTgxlAddress` is followed by the `tuner` delta carrying the saved address, then a blank host, saved and shown as blank, and the address again; `verbs-pgxl-control` (which requires `remotePgxlControlVersion` 4) does the same for `scanPgxlLan` and `setPgxlAddress` (the `amplifier` delta, and the blank host), and its `setPgxlOperate` is refused on the static station, which has no amp connected ("The Core is not connected to the Power Genius."); `verbs-rfkit` (which requires `remoteRfKitControlVersion` 2) connects, disconnects and switches the RF-Kit, and `verbs-rfkit-control` (which requires `remoteRfKitControlVersion` 4) invokes `setRfKitOperate`, `setRfKitAntenna` and `setRfKitTciMode`, refused on the static station with no amp admitted ("The Core is not connected to the RF-Kit amplifier."), and its accepted `setRfKitAddress` is followed by the `rfkit` delta carrying the saved address, then the blank host and the address again. A version's new verbs go in a fixture of their own, so an app at the older version still runs the older file |

`tst_link_conformance_session` also checks that every verb in the
`commands` table is invoked both ways by some fixture, and that the two
legs of each get different answers.

### 16.4 Media vectors

`tst_link_conformance_regen` writes every vector from the station's own
encoders and fixed inputs, into `NEREUS_LINK_REGEN_OUT` only. Each `.bin`
is one packet as it travels.

**Decoding a vector.** A runner decodes a vector on a fresh decoder. When
`expect` holds `"after": ["<fixture id>", ...]`, it first decodes each
named vector's bytes, in order, on that same decoder, without checking
their own expectations, then decodes the vector itself and compares only
its own expectation. `after` is not followed further: a named vector's
own `after` is not decoded first (it is not recursive), so a vector's
`after` lists every packet before it, in order, that the decoder must
have seen (`nsdc1-keyframe-after-loss` names `nsdc1-full` itself, not a
vector that names it). A vector without `after` decodes on a fresh decoder
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
| `opus` | `opus-1` to `opus-4`: four consecutive RTP packets of the speakers' mix at the station's settings (48 kHz stereo, 1920 samples per packet, 24000 bit/s, wideband, the Core's default `audio_bitrate`). A receiver stream runs Opus at 48000 bit/s fullband whatever `audio_bitrate` says (media control document, receiver audio) and is read by the same decoder; its `encoder` object reports the rate | the packets before it | `status` `accepted`, `sequence`, `timestamp`, `channels` 2, `bandwidth` 1103 (Opus wideband), `samplesPerChannel` 1920, and `pcm16`, the station decoder's output as 16-bit values (`round(sample * 32767)`); `ssrc` is the packets' RTP source, which the decoder is given; `tolerance` `{"minSnrDb": 60}` |

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
  English with no protocol terms. Every reason the station sends (the
  `reason` of `auth.result`, `session.end`, `command.result`,
  `property.result`, `settings.reject` and the display's `rejected` and
  `allocation-result`) passes `OperatorWording::isPlain` and names no
  function, class, requirement or phase; `tst_station_reason_wording`
  checks the sources that word them (every reason literal, a reason of one
  word, and what `.arg()` inserts into one; a new function that words a
  reason, in any file, fails until it is scanned) and every reason the
  session fixtures record. An app shows these as sent. Text the station
  sends as a property value that an app shows as it arrives is held to the
  same rule: the `connectionError` of the `tuner`, `amplifier` and `rfkit`
  objects, a slice's `nnrStatus` and `nnrLastError`, the radio's
  `rxFilter0Reason`, `rxFilter1Reason`, `settingsSaveError`,
  `receiveLayoutRestoreMessage` and `fourO3AListenerError`, the
  `stationTci` object's `error`, the `amplifier` object's
  `efficiencyText` (the Power Genius's own reading, passed on as it
  reports it), the `accessorySettings` object's `pgxlAnswer` and
  `tgxlAnswer`, and `pureSignalSettings`' `lastLoadError` (the same test
  reads the sources that write them). A library's or the system's own
  error text (a socket's, a file's) is never sent as one of these: the
  station words it and logs the library's text. A reason passed on
  without words of its own (a variable, a call) is named in the test with
  the source its words come from. Codes are not reasons and keep their
  spelling: an audio context's `reason` (`client-disabled`,
  `receiver-limit`, ...), `displayBudgetReason`, and the display retire
  reasons "slice removed" and "slice stream binding changed", which
  windows in use compare as they are.
