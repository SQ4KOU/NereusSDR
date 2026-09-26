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
| Display extras (the subscription fields that ask the Core for the peak blobs, peak hold, noise floor, waterfall levels, normalise, calibration and averaging, and the NSDX v1 datagram beside each NSDC frame) | [2026-09-23-display-extras-v1.md](2026-09-23-display-extras-v1.md) |
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
  a client certificate (`setPeerVerifyMode(QSslSocket::VerifyNone)`); a
  client proves who it is with its own device key (section 3.5) or, on a
  Core upgraded from before paired devices, the token (section 3.3).
- The control port is set by `remote_port` in `nereusd.conf`
  (`DaemonConfig.h`). With neither `remote_port` nor `remote_bind` in the
  file (or no file), the station listens on TCP 47910
  (`DaemonConfig::kDefaultRemotePort`) on every interface, IPv4 and IPv6
  (`remote_bind` empty: `QHostAddress::Any`, `DaemonApp::listenerAddressFor`),
  and announces itself (section 14). A file that sets either key keeps the
  meaning it had before this default: the key it leaves out is 0 (off) or
  `127.0.0.1` (`kExplicitConfigRemotePort`, `kExplicitConfigRemoteBind`), and
  a `remote_port` that is not a number leaves the listener off.
  `remote_port = 0` turns the listener and the announcement off.
- The opening request (the HTTP upgrade) is read by the station before
  Qt sees it (`StationOpeningGate`, in front of the `QWebSocketServer`).
  The station routes nothing by `Host`, so it accepts any `Host` in these
  forms: an IPv6 address in brackets, with or without a port
  (`[2001:db8::1]:47910`, `[::1]`); an IPv6 address without brackets, with
  or without a port (`2001:db8::1:47910`, `::1`, what Apple's
  `NWProtocolWebSocket` sends for an IPv6 URL); an IPv4 address with or
  without a port; and a host name (labels of 1 to 63 letters, digits, `-`
  and `_`, split by single dots, none starting or ending with `-`), with or
  without a port. An IPv6 zone (`%en0`) is dropped. A value
  without brackets that reads both as an address and as an address and a
  port (`::1:8080`) is taken as an address; the station reads nothing from
  it either way. Qt itself cannot read an unbracketed IPv6 `Host` and
  answers such a request with nothing, so the station writes the `Host`
  back in brackets before handing the request on.
- A request the station cannot read gets `400 Bad Request` with
  `Connection: close` and a one-line plain text body, and the connection
  closes: no `Host`, more than one `Host`, a `Host` in none of the forms
  above (or one Qt's URL parser finds no host in), a request line other
  than `GET <path> HTTP/1.1`, a header line that is not `name: value`
  with a token name (a folded line, one starting with a space or tab,
  included), a control character other than a tab in a value, more than
  100 header lines, a first `Upgrade` header that is not exactly
  `websocket`, a first `Connection` header without `Upgrade`, a first
  `Sec-WebSocket-Key` that is not 16 bytes, or a `Sec-WebSocket-Version`
  that is missing or is not a comma-separated list of numbers. A head
  longer than 8192 bytes (`kMaxRequestHeadBytes`) gets the same. These
  follow how Qt reads the request, so that each request the station
  passes on is one Qt answers. A `Sec-WebSocket-Version` number the
  station does not speak (`8`) is Qt's to answer: its own `400`, then the
  close. If Qt closes a request without writing anything, for any other
  reason, the station writes the same `400` first. No complete opening
  request ends without an answer; one never finished is closed at the
  deadline below.
- The whole opening, from the TCP accept through TLS to the `101`, must
  finish within 10 s (`StationServer::kDefaultOpeningDeadlineMs`, Qt's own
  default handshake timeout); a connection that has not opened by then is
  closed. `tst_station_ws_host` sends each form over TLS to the real
  listener.
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

- A station no longer creates a token. A Core upgraded from before paired
  devices keeps the one it has in `station-token` in its profile directory:
  32 random bytes (256 bits), written as base64url without padding, 43
  characters (`TokenStore.cpp`). The token is loaded, never generated.
- While it is active the Core counts as claimed (`DeviceStore::isClaimed`),
  so no stranger can pair with it, and windows that sign in with it keep
  working. A window that sends its `device` block with the token enrols its
  key in the same step (section 3.5) and signs in by key from then on.
- Retiring the token (`TokenStore::retire`: the file is deleted) ends it for
  good. A paired device retires it with `station.retireToken` (section 9.1),
  which the Core refuses until at least one device is paired; the Core then
  ends every connection still signed in by token, including one that also
  enrolled its device key (section 12.4). On a Core with no token (a new one, or one whose token was retired)
  a sign-in with a token, empty or not, is refused with `auth.result`
  accepted false, "This Core uses paired devices. Pair this device first.",
  `retryable` false, `code` `pairingRequired`, and the token's limiter is
  not consulted.
- After 5 consecutive failed checks (`kDefaultMaxFailuresPerLockout`) the
  station refuses every token check, including a correct token, for 60 s
  (`kDefaultLockoutMs` 60000). That lockout is global for the token, not per
  peer; it never refuses a device key (section 3.5).
- The station never logs a candidate token.

### 3.4 The identity key

The Core has its own identity key, separate from the TLS key
(`StationIdentity`, R-IOS-08; the pairing design, section 3.1):

- ECDSA P-256, created on the first start and kept in
  `station-identity.pem` (PKCS#8, PEM) in the Core's profile directory, mode
  0600, written atomically; reused on every later start. A file that exists
  but does not hold a P-256 private key is refused and never replaced, and
  the Core then does not listen. The first start prints the key file's path
  and the TLS pin to standard output (not the log), with the prompt to back
  the file up: losing it means every paired device pairs again
  (`StationServer::formatFirstRunBanner`).
- A public key travels as base64url, without padding, of its
  SubjectPublicKeyInfo DER: 91 bytes for a P-256 key with its point
  uncompressed. A key's fingerprint is SHA-256 of that DER. A signature is
  ECDSA P-256 over SHA-256, raw `r || s`, 64 bytes, base64url. The station
  accepts only a canonical 91-byte P-256 key (`StationIdentity::isP256Spki`)
  and strict base64url (no padding, only `A-Z a-z 0-9 - _`, the unused bits
  of the last character zero).
- The certificate binding is the identity key's signature over
  `"NereusSDR cert-binding v1\n" || SHA-256(TLS certificate DER)`
  (`StationIdentity::certBinding`). The SHA-256 is the pin's 32 bytes
  (section 3.2).
- The station's `hello` carries `identity`
  `{"publicKey": <key>, "certBinding": <binding>}` and `challenge`, 32
  random bytes from the operating system's generator, base64url, new for
  every connection (`DeviceAuthenticator::newChallenge`), and declares
  `features.deviceAuth` 1. A device that holds the Core's key checks the
  binding against the certificate its connection presents before it signs
  anything; a mismatch is the client's own `identityChanged` (section
  12.4).

### 3.5 Device sign-in

Each paired device holds its own P-256 key. The Core keeps the paired
devices in `paired-devices.json` in its profile directory, mode 0600,
written atomically (`DeviceStore`): each device's `id` (the fingerprint of
its key), key, name, kind (`phone`, `tablet` or `computer`), when it was
paired and last seen, its last address, whether it was enrolled through
the token, and its short name (below; `""` until a sign-in brings one, and
absent from a list written before it). A file that cannot be read fails closed: no device signs in, the
file is never overwritten, and the Core counts as claimed.

A device signs in with `auth.request`, `token` `""`, and `device`
`{"id", "publicKey", "name", "kind", "signature"}` (all strings;
`SessionDeviceBlock`), plus an optional string `shortName`, where
`signature` is its key's signature over the transcript
(`DeviceAuthenticator::transcript`):

```
"NereusSDR device-auth v1\n" || challenge (32 bytes)
  || SHA-256(TLS certificate DER) || SHA-256(Core identity SPKI DER)
  || SHA-256(device SPKI DER)
```

The station admits it (`DeviceAuthenticator::verify`) only when the block
is well formed, the `id` is the fingerprint of `publicKey`, the signature
verifies over this connection's challenge and this Core's certificate and
key, and the device is in the paired devices with that key. A client sends
`device` only to a station whose `hello` declares `deviceAuth` 1.

**The short name.** `shortName` is the device's own short name, for the
places a screen has room for one word; a client sends it at every sign-in,
with no gate. It is at most 32 bytes of UTF-8 (`shortNameMaxBytes`,
`DeviceStore::kMaxShortNameBytes`), counted as the 64-byte name cap is, and
it is validated as a name is (`DeviceStore::isValidShortName`: not empty
after trimming, no control, format, line-separator or paragraph-separator
characters, valid UTF-8). It is the operator's own words, so it is not held
to the Core's wording rules: "Grant's iPhone" passes. It sits outside the
signed transcript, as `name` does. The Core stores it with the device and
replaces it at each sign-in that carries a usable one; an absent or
unusable one changes nothing and refuses nothing. When present it must be a
string, or the message is malformed. Where the Core sends a device's name
and short name (the `devices` and `connectedDevices` objects, section 7.1)
it numbers them on collisions and gives a device with no usable short name
its kind's word ("Phone", "Tablet", "Computer"). A token sign-in that enrols its key
(below) stores it too. The desktop sends the computer's short host name,
trimmed to the cap (`ClientDeviceIdentity::machineShortName`). Pairing does
not carry it: `pair.start`'s `device` block and the code-mode box keep
`{"publicKey", "name", "kind"}`, and a device signs in straight after
pairing.

| Case | `auth.result` reason | `retryable` | `code` |
| --- | --- | --- | --- |
| Admitted | `""`, accepted true | false | none |
| A well-formed proof from a key the Core has not paired | "This device is not paired with this Core. Pair it first." | false | `deviceNotPaired` |
| A bad signature, one over another connection's challenge, one binding another certificate or another Core's key, an `id` that is not its key's fingerprint, a malformed block | "This device could not prove it is paired with this Core." | false | `deviceProofFailed` |
| Rate limited (below) | "The Core is refusing sign-ins from this device for a while after too many failed ones. Try again later." | true | none |

Each refusal is followed by the close, as for a token (section 12.4).

**Rate limits.** Failed device sign-ins are counted per source address and
per device id: 10 within 60 s (`kMaxFailures`, `kWindowMs`) refuse that
address, or that id, for 60 s (`kLockoutMs`). A sign-in refused while
limited is not counted again. A connection through the relay has no
address of its own (`SessionTransport::peerAddress` is empty there), so the
limit applies per device id and per relay introduction. The device limiter
never consults the token's (or, later, the pairing code's), and they never
consult it: wrong tokens do not lock out a device's key, and failed device
sign-ins do not lock out the token.

**Enrolment through the token.** On a Core upgraded with a token, a window
that sends its `device` block together with the right `token` is admitted
by the token (the desktop sends it when the Core's hello carries its
identity and `deviceAuth`, its binding verifies for the certificate, and
the connection's pin was checked; iPhone app plan Task 18); its block must still prove its key over this connection's
transcript (`DeviceAuthenticator::verifyPossession`), or the sign-in is
refused `deviceProofFailed` and nothing is enrolled. A key not yet paired is
added as kind `computer`, `enrolledThroughToken` true, named by its block
(or "Computer" when the name is not usable). From then on the window signs
in with its key alone, nothing typed.

**Last seen.** Every authenticated connection of a paired device records
the time and the peer's address (empty over the relay, never the relay's).

### 3.6 Pairing

A device that is not paired pairs on a connection of its own (iPhone app
plan Task 14; the pairing design, sections 4.2, 4.3 and 4.5). After the
station's `hello` it sends its own `hello` and then `pair.start` in place of
`auth.request`, and only to a station whose `hello` declares
`features.pairing` 1 (`SpakeExchange::isAvailable()` and a usable identity
key, the same condition as `deviceAuth`). The station declares it; it never
asks it of a client. Nothing on a pairing connection signs in: the
connection ends after `pair.accept`, the station's `pair.confirm` or
`pair.fail`, with no `session.end`, and the device then signs in by key on
a new connection (section 3.5). An `auth.request` after `pair.start`, or a
`pair.*` message out of turn, ends the connection with `session.end`
`protocolError`. The handshake deadline (section 12.2) applies.

**The pairing window** (`PairingWindow`) is the station's, not any
connection's:

- `OpenUnclaimed`: the Core has no paired device and no active token
  (`DeviceStore::isClaimed` false). Open with no timer, but with the
  attempt ceiling below. One tap pairs, and so does the code.
- `ClosedClaimed`: the Core is claimed and the window is shut. Nothing
  pairs.
- `OpenReopened`: reopened on a claimed Core, from the Core's console or
  by a paired device (`pairing.open`, section 9.1). Only the code pairs.
  It closes after one successful pairing, on `pairing.close`, or 10
  minutes after it opened (`PairingWindow::kReopenedLifetimeMs`).
- `ClosedUnclaimed`: an unclaimed Core whose window the attempt ceiling
  closed. Nothing pairs until the console reopens it (`nereusd pairing
  open`); no device is paired to do it, so physical access decides.

**The attempt ceiling.** Five codes burned in a row on a direct
connection (`PairingWindow::kMaxConsecutiveFailures`) close any open
window, reopened or unclaimed. A closed window opens again only from the
Core's console or, on a claimed Core, from a paired device
(`pairing.open`). Reopening starts afresh: no failures counted and no wait,
so the code is there at once.

**Codes burned through the rendezvous** (a pairing mailbox, section 19)
never count toward the ceiling and never close the window (the operator's
ruling of 2026-09-26). Five of them in a row pause pairing through the
rendezvous instead: 1 minute (`PairingWindow::kFirstServicePauseMs`), then
twice as long each time it is hit again with no pairing in between, at
most 60 minutes (`PairingWindow::kMaxServicePauseMs`). While paused, a
mailbox's `pair.start` gets `pair.fail` with `retryAfterMs` the time left,
before any code is taken, so it burns nothing. Pairing on a direct
connection stays open throughout. A pairing, or reopening the window, ends
the pause and starts over at 1 minute. A code burned either way rotates
after the same wait.

The first pairing closes an unclaimed window, for good. `devices.revoke`
never removes the last device while no token is active (section 9.1), so a
claimed Core becomes unclaimed again only through its console's `reset
--unclaimed --yes`, and its window then opens.

**The code** is a number and two words, `<nameplate>-<word>-<word>`, for
example `7-anvil-harbor`. The words come from
`resources/pairing-words-v1.txt`: 256 lowercase words of 4 to 7 letters,
no two within one edit of each other and no two alike in sound. The
number is the rendezvous nameplate: while the Core is registered with the
rendezvous (section 19) it holds a nameplate there while its pairing window
is open and shows that number. A Core that has never held one picks a
number from 1 to 99 (`PairingWindow::kLocalNameplateMax`). A Core that
loses the rendezvous keeps showing the number it last held, since the code
still pairs on a direct connection (the number is part of the password, not
an address there), and shows the new number when it registers again and
claims one; a change makes a new code unless the code is being tried.
Both ends normalise a typed code before they use it
(`PairingCode::normalise`). Normalising lowercases and trims, drops any
leading zeros of the number, and joins the three parts with single
hyphens, whatever separators were typed. It refuses anything that is not
a number from 1 to 999999 and two words of the list. The normalised text,
as UTF-8, is the password.

The code is given on request by the Core's console (`nereusd pairing
show`, over its owner-only control socket), on its status page while the
Core is unclaimed, and (from iPhone app plan Task 49) on the Remote Access
page of a desktop running the Core. It is never printed to standard output
and never logged: on a packaged Core both land in the journal. The link carries it only to a connection signed in with
a paired device's own key: in the `devices` object's `pairingCode` and in
`pairing.open`'s result (section 7.1). A connection signed in with the
token receives `""` in `pairingCode`, even when its hello declares
`deviceAuth`, and its `pairing.open` is refused (section 9.1). A
fixture writes the code as `"$string"`, never literally.

**Single use, and the wait.** The station commits to the code when the
device's step 1 arrives (`PairingWindow::takeCode`). From then on the code
is either paired with or burned: a wrong code, a `pair.fail` from the
device, or the connection ending first all burn it. Only one exchange
holds the code at a time, and a second one's step 1 is refused. After a
burned code the next one appears after 5 s. The wait doubles after each
consecutive failure (`kFirstRetryMs`; 5, 10, 20 and 40 s, since the fifth
burn closes the window), and a successful pairing resets it. While no code is shown, `pair.start` in
code mode is refused, and `retryAfterMs` says when the next one appears.

**The messages** (every binary value is base64url without padding):

| Kind | From | Keys |
| --- | --- | --- |
| `pair.start` | device | `mode` `"lan"` or `"code"`; `device` `{"publicKey", "name", "kind"}`: the device's P-256 key as in section 3.4, a name, and `phone`, `tablet` or `computer` |
| `pair.accept` | station | `identity` `{"publicKey", "certBinding"}` as in the hello (section 3.4); `label`, the Core's label (`devices`' `stationLabel`) |
| `pair.spake` | both | `step`, 0 to 3; `data`, that step's bytes |
| `pair.confirm` | both | `box`: a 24-byte nonce, then the XChaCha20-Poly1305 (IETF) ciphertext and tag |
| `pair.fail` | station, and the device after its own step 3 fails | `reason`, plain words; `retryAfterMs`, a whole number of milliseconds, 0 to 2147483647 (0: now, or not with this Core as it stands) |

**One tap** (`mode` `"lan"`). The station adds the device when the Core
is unclaimed (`OpenUnclaimed`), `pairing_lan_click` is `allow`, and the
connection's address is on one of the Core's directly connected networks
(`StationServer::isOnDirectNetwork`: a loopback address, or one inside the
subnet of an address of a running interface). A relayed connection has no
address of its own, so it never qualifies. The station then sends
`pair.accept` and ends the connection.

One tap trusts every network the Core's computer is on, not only its LAN:
a VPN or overlay it has joined (ZeroTier, Tailscale, WireGuard, a Docker
bridge) is a running interface too, and a loopback address counts, so a
local reverse proxy or SSH tunnel qualifies. Anyone on any of them can
claim an unclaimed Core with one tap. `pairing_lan_click = deny` is the
remedy: every device then pairs with the code (the sample configuration,
`packaging/nereusd.conf.sample`, says so).

**The code** (`mode` `"code"`), SPAKE2+EE over libsodium
(`SpakeExchange`). Its fixed values are the client identity
`"nereussdr-device-v1"` and the server identity `"nereussdr-station-v1"`.
Password hashing uses libsodium's default algorithm (Argon2id) with
`crypto_pwhash_OPSLIMIT_INTERACTIVE` and
`crypto_pwhash_MEMLIMIT_INTERACTIVE`:

1. Station: `pair.spake` step 0, 36 bytes: the hash parameters and salt.
   The station hashes each code once, when the first code-mode pairing
   asks for it, on a worker thread (`StationServer::startPairingHash`), so
   the hash never holds up the Core's event loop or another device's
   connection; step 0 goes out when the hash is back.
2. Device: checks that step 0 names exactly those parameters
   (`crypto_spake_validate_public_data`) before it hashes the code, then
   sends step 1, 32 bytes. The station takes the code here.
3. Station: step 2, 64 bytes, once step 1 is a valid point
   (`crypto_core_ed25519_is_valid_point`: canonical, on the curve, on the
   main subgroup, not of small order); one that is not is a malformed
   step 1 and burns the code.
4. Device: step 3, 32 bytes. When the codes differ, the device's step 3
   fails; it sends `pair.fail` instead, and the station burns the code and
   answers with its own `pair.fail`. Otherwise the device sends step 3 and
   then its `pair.confirm`, whose box is sealed with the shared key
   `client_sk` around the compact JSON `{"publicKey", "name", "kind"}`.
5. Station: checks step 3 (spake2-ee's step 4). A mismatch burns the code
   and sends `pair.fail`. When the window has closed since step 1 (closed,
   its ten minutes over, or closed and reopened), it burns the code and
   sends `pair.fail` as for a closed window. It opens the device's box. The box's
   `publicKey` must be the one `pair.start` named, and the box's `name`
   and `kind` win over the plain ones. The station adds the device and
   answers with its own `pair.confirm`: a box sealed with `server_sk`
   around `{"identity": {"publicKey", "certBinding"}, "label"}`. Then it
   ends the connection.

Whoever carries these messages (the rendezvous, later) learns nothing it
could test guesses against. Each exchange is one guess, and the code burns
after it.

| Refusal | `pair.fail` reason | `retryAfterMs` |
| --- | --- | --- |
| No pairing on this Core (no identity key or cryptography) | "This Core cannot pair new devices." | 0 |
| A key that is not a P-256 key, or an unusable name or kind (in `pair.start` or the box), or a box that does not open or names another key | "The Core could not read this device's details. Update this app." | 0 (the wait, from the box on) |
| The key is paired already | "This device is already paired with this Core. Connect to it instead." | 0 |
| The window is closed (at `pair.start`, or at the confirm step when it closed after the exchange took the code, which burns it) | "This Core is not taking new devices. Open pairing on the Core or on a paired device first." | 0 |
| One tap on a claimed Core | "One tap pairs only a Core with no paired devices. Use the pairing code the Core shows." | 0 |
| One tap with `pairing_lan_click = deny` | "This Core pairs only with its code. Use the pairing code the Core shows." | 0 |
| One tap from off the Core's networks | "One tap works only on the Core's own network. Use the pairing code the Core shows." | 0 |
| Another exchange holds the code | "Another device is pairing with this Core right now. Try again shortly." | 5000 |
| No code shown yet (the wait) | "The Core is waiting before it shows a new pairing code. Try again when the new code appears." | until the next code |
| The code changed between step 0 and step 1 | "The pairing code changed. Enter the code the Core shows now." | 0, or the wait |
| A malformed step 1 | "The pairing code was not accepted. A new code will appear on the Core." | the wait |
| A wrong code (either side) | "The pairing code was not right. A new code will appear on the Core." | the wait |
| The device could not be saved | "The Core could not save this device. Try again." | 0, or the wait once the code was taken |

`nereus_pairing_peer` (`tests/tools/`) is the station's side of one
pairing over standard input and output, one message per line, for the
cross-implementation tests. It prints `{"type":"peer.ready","code",
"certSha256"}` first and `{"type":"peer.done","paired","devices"}` last.
Its options are `--lan-deny`, `--address <ip>` (default `127.0.0.1`) and
`--claimed` (a device paired first, the window reopened). The live
exchange cannot be scripted in a fixture, so this tool proves it.

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
| `auth.request` | `token` (string), `type` (string) | `device` (object) |
| `auth.result` | `accepted` (boolean), `reason` (string), `type` (string) | `code` (string), `retryable` (boolean) |
| `capabilities` | `properties` (array), `type` (string) | none |
| `command.invoke` | `args` (array), `id` (number), `type` (string), `verb` (string) | none |
| `command.result` | `accepted` (boolean), `affected` (array), `id` (number), `reason` (string), `type` (string), `verb` (string) | `values` (array) |
| `confirm.request` | `affected` (array), `expiresInMs` (number), `id` (number), `kind` (string), `reason` (string), `type` (string) | `change` (object), `choices` (array), `forCommandId` (number), `forSettingsKey` (string), `forWriteId` (number), `holder` (object) |
| `delta` | `key` (string), `properties` (array), `type` (string) | none |
| `hello` | `major` (number), `minor` (number), `peer` (string), `settingsSchema` (number), `type` (string) | `challenge` (string), `features` (object), `identity` (object), `majors` (array) |
| `media.control` | `payload` (object), `type` (string) | none |
| `notice` | `id` (number), `kind` (string), `reason` (string), `secondsAgo` (number), `takeBack` (boolean), `type` (string) | `byDeviceId` (string), `byKind` (string), `byName` (string), `byShortName` (string), `bySource` (string), `change` (object), `slices` (array) |
| `object.create` | `class` (string), `key` (string), `properties` (array), `type` (string) | none |
| `object.destroy` | `class` (string), `key` (string), `type` (string) | none |
| `pair.accept` | `identity` (object), `label` (string), `type` (string) | none |
| `pair.confirm` | `box` (string), `type` (string) | none |
| `pair.fail` | `reason` (string), `retryAfterMs` (number), `type` (string) | none |
| `pair.spake` | `data` (string), `step` (number), `type` (string) | none |
| `pair.start` | `device` (object), `mode` (string), `type` (string) | none |
| `property.result` | `key` (string), `results` (array), `type` (string), `writeId` (number) | none |
| `property.write` | `key` (string), `properties` (array), `type` (string) | `writeId` (number) |
| `schema` | `class` (string), `fields` (array), `type` (string) | none |
| `session.end` | `reason` (string), `type` (string) | `code` (string), `retryable` (boolean) |
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
   exception: a station already holding its limit of connections (24,
   `kMaxConcurrentPeers`, section 15) sends no `hello`. The first and only
   message on the new connection is `session.end` "The Core already has
   as many connections as it allows. Try again shortly.", `retryable`
   true, and the station closes it. A client handles a `session.end` in
   place of the `hello` as it would any other (fixture
   `connection-limit`).
3. The client answers with its own `hello`, naming the major it chose,
   then `auth.request`: a paired device's `device` block with `token` `""`
   (section 3.5), or the token of a Core upgraded from before paired
   devices, with or without the window's `device` block to enrol
   (`StationClient.cpp` sends the token after its pin check, and to a Core
   it paired with only its `device` block, after checking the Core's
   identity and certificate binding). A device
   that is not paired sends `pair.start` instead, and the connection
   pairs and ends (section 3.6).
4. The station sends `auth.result`. On success it decides who is let in
   (`StationServer::admit`, iPhone app plan Task 71): up to four devices
   hold places on one Core at once (`kMaxDeviceSessions`, section 12.3). A
   device that already holds a place, live or away (section 12.4), is let
   in at once and its older connection ends with `session.end` "This device
   connected again.", `retryable` false, `code` `sameDevice`; with a place
   free the device is let in; with every place taken the station sends
   `session.end` "The Core already has four devices connected.",
   `retryable` true, no code, and closes (a window that did not declare
   `sessionHolder` with `deviceAuth`, which cannot answer the fifth-device
   question, is told "The Core is full. Update NereusSDR to take a device's place, or try again later." instead, `retryable` true, no code). No sign-in ever ends another
   device's session. A device let in gets, in this order
   (`StationServer::promoteToSession`):
   - `capabilities` (section 6);
   - `settings.snapshot`: every station-scoped setting (section 8);
   - one `schema` per mirrored class, in the order the classes are first
     watched (`MirrorView::attach`);
   - one `object.create` per object, each with its full property set: the
     fixed objects first, then one per slice the device owns and, to a
     device with `sessionHolderVersion` 1, one `marker:<id>` per slice of
     another device (section 7.1, iPhone app plan Task 73);
   - `snapshot.complete`.

   These go to the device let in alone, from its own view of the station's
   state (iPhone app plan Task 72): another device's session receives none
   of them, and what the station had still to send another device is sent
   to it as before.
5. From then on the station sends `delta`, `object.create` and
   `object.destroy` as its state changes, and the client may send property
   writes, settings writes and commands. With several devices on one Core,
   each message goes where section 12.4's routing says.

A `hello` carries `major`, `minor`, `settingsSchema` (the sender's settings
schema version) and `peer` (a name for the sending program). A difference
in `settingsSchema` is logged by both ends and is not a refusal. It may
also carry `majors` and `features` (sections 6.1 and 6.2); the station's
`hello` always carries both, and `identity` and `challenge` (section 3.4)
when its identity key is usable.

The station refuses, with `session.end`, `retryable` false and `code`
`protocolError`: a second `hello` ("This app started connecting twice on one
connection."), `auth.request` before `hello` or a second `auth.request`
("This app sent its pairing token out of order."), and any other message
before authentication ("This app sent a request before the Core had
accepted its pairing token.").

### 5.2 Resends during a session

- **A changed transmit permission** (iPhone app plan Task 34). When a
  session's `txPermitted` changes (its `snapshot.complete` went out, the
  holder of transmit changed, the Core's `remote_transmit` changed), the
  station sends it `capabilities` again. It does not resend
  `settings.snapshot`.

- **A late radio.** A station can accept a client before its radio is
  found. When the radio connects, the station sends `capabilities` and
  `settings.snapshot` again, one event-loop turn later, to every session
  let in when the radio arrived, each with its own capabilities
  (`currentRadioChanged` in the `StationServer` constructor). Settings are scoped to the connected radio,
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
client asks `StationClient::stationDeclares(feature, minVersion)`. The station
declares `deviceAuth` 1 (section 3.5), `pairing` 1 (section 3.6) and
`sessionHolder` 1 (below) when its identity key is usable.

**`remoteTx` 1** (iPhone app plan Tasks 34 and 35): the client understands
remote transmit: `txPermitted`, the transmit refusals, `tx.setTxSlice` and
keying (`tx.key`, `tx.unkey`, `tx.tune`, `tx.twoTone`, section 18.6). A peer that declares it at minor 11 is sent
`remoteTxVersion` (section 6.3); `txPermitted` is true only for a peer that
declares it. It is also sent `txStateVersion` and the `txState` object
(iPhone app plan Task 39, section 18.8). The station does not declare it.
The desktop's remote window declares it (desktop remote transmit): its
MOX, TUNE, two-tone, microphone, VOX and TCI programs transmit through the
Core, and its transmit meters read `txState`.

**`sessionHolder` 1** (iPhone app plan Task 71; the several-devices
design, ruling 10.1): the Core admits up to four devices at once (section
5.1). A client declares it only together with `deviceAuth` 1 or later, and
the station treats `sessionHolder` without `deviceAuth` as not declared.
A client that declares it receives `sessionHolderVersion` in its
capabilities (section 6.3) and the `connectedDevices` object (section
7.1), and may send `session.leave` (section 9.1). A client that does not
sees exactly the wire it was built for. The desktop client declares `deviceAuth` 1
when it holds its own device key (`device-identity.pem` in its profile
directory, `ClientDeviceIdentity`; it always does unless that file cannot
be read) and sends `{}` otherwise (iPhone app plan Task 18). A client's
`deviceAuth` 1
(or later) also asks for the `devices` object and its commands (section
7.1): the station sends them to no other peer, so a window that declares
nothing sees exactly the wire it was built for. A client never sends a
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

What the several-devices design sends in place of `capabilities` (the
fifth device's question, which a later version adds) is gated by the hello
feature `sessionHolder` alone, in both ends' `hello`, since no capability
has arrived by then; everything else it brings uses the two keys above,
with `sessionHolderVersion`.

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
| `spectrumGrantVersion` | 2 |
| `remoteDisplayBudgetVersion` | 1 |
| `remoteCtunVersion` | 1 |
| `stationTelemetryVersion` | 5 |
| `remoteTgxlConfigVersion` | 1 |
| `remoteFourO3AControlVersion` | 1 |
| `wdspVersion` | 210 |
| `wdspCompatibilityVersion` | 1 |
| `nnrVersion` | 1 |
| `psAlgorithmVersion` | 3 |
| `propertyResultVersion` | 1 |
| `dspAssetVersion` | 4 |
| `psDisplayVersion` | 1 |
| `notchControlVersion` | 2 |
| `audioProfileVersion` | 1 |
| `audioClockVersion` | 1 |
| `receiverAudioVersion` | 1 |
| `headphonesMixVersion` | 1 |
| `radioHardwareVersion` | 7 |
| `remotePgxlControlVersion` | 4 |
| `remoteRfKitControlVersion` | 4 |
| `stationTciVersion` | 1 |
| `accessoryDataVersion` | 3 |
| `remoteTgxlControlVersion` | 4 |
| `stationIdentityVersion` | 1 |
| `deviceAdminVersion` | 1 |
| `pairingVersion` | 1 |
| `stationCatalogVersion` | 1 |
| `displayExtrasVersion` | 2 |
| `transmitSettingsVersion` | 8 |
| `bandSelectVersion` | 1 |
| `meterReadingsVersion` | 1 |
| `dspInfoVersion` | 1 |
| `sessionHolderVersion` | 1 |
| `remoteTxVersion` | 2 |
| `txStateVersion` | 2 |

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
  PA readings and radio link quality (section 10); at 5 (parity Task 14)
  also the Core's Hermes Lite 2 link (`hl2*`, section 10).
- `remoteTgxlConfigVersion`, `remoteFourO3AControlVersion`: 0 unless the
  Core owns its accessories.
- `wdspVersion`, `wdspCompatibilityVersion`, `nnrVersion`,
  `psAlgorithmVersion`, `dspAssetVersion`: 0 on a station built without
  WDSP. `psDisplayVersion` also needs media.
- `remoteCtunVersion`, `propertyResultVersion` and `notchControlVersion`
  are never 0.
- `dspAssetVersion`: 1 carries the NNR model assets (`dspAssets.*`,
  `nnr.applyModelSelection`); 2 adds the Core's NR3 model
  (`dspAssets.selectNr3Model`, with `nr3ModelAsset`, `nr3ModelStatus` and
  `nr3Runnable` on `DspAssetService`); 3 adds `dfnrRunnable` and
  `dfnrModelStatus` on `DspAssetService`: whether the Core can run DFNR
  (the build has it and its DeepFilterNet model file is there and loaded)
  and, when it cannot, the plain reason. While it is false a window shows
  DFNR disabled and refuses turning it on with the reason. 4 adds
  `mnrRunnable` and `mnrStatus` on `DspAssetService`, the same pair for
  MNR, which runs only on a Mac Core: a window on any computer shows MNR
  disabled with `mnrStatus` while `mnrRunnable` is false. These pairs are
  the one source a window has for which noise reduction its Core runs: on
  the VFO flag, its quick controls, Setup > DSP > NR/ANF and DSP > NR,
  never by the window's own build (a Mac window on a Linux Core offers no
  MNR, and a Linux window on a Mac Core does). On a Core below 3 (for
  DFNR) or below 4 (for MNR), which does not send the pair, a window shows
  that filter disabled with "This Core does not say which noise reduction
  it can run. Updating the Core may help." and refuses turning it on. BNR carries no pair and is not offered: no build has it, so no
  window shows a BNR control. A BNR selection (`activeNr` 6, which keeps
  its value) is refused by the Core and by a window with a plain reason,
  and a slice holding one turns off.
- `notchControlVersion`: 2. At 1 the Core owns the notch list (the
  `notches` object, `notch.add`, `notch.move`, `notch.setActive` and
  `notch.delete`); 2 adds `notch.addAtSlice`, the desktop's +TNF on a
  slice (section 9.1). The four earlier verbs need 1, so a client that
  compares the version as a minimum reads 2 exactly as it read 1. It is
  sent at every minor, as before; the notch verbs need agreed minor 5.
- `spectrumGrantVersion`: 2 with media. At 1 a spectrum `context`
  reports what the Core granted the endpoint (`grantedFftSize`,
  `grantedTier`, `requestedPixels`, `grantedPixels`, `limit`); 2 (parity
  Task 17, R-R3-01) adds the `subscribe` field `decimation`, a whole
  number 1 to 32 applied to the endpoint's engine (the remote media
  control document, "Display subscriptions"). A client that compares the
  version as a minimum reads 2 as it read 1; a window told less than 2
  does not send `decimation`, and a Core refuses it from a peer below the
  grant minor as a request it cannot read. An endpoint beside another on
  its engine runs at the engine's decimation, and its context's `limit` is
  then `shared` (no new field; a client that never sends `decimation` asks
  for 1 and is told `shared` beside a decimated neighbour).
- `radioHardwareVersion`: sent only at agreed minor 11. 0 without the step
  attenuator bound; 1 with it; 2 with the Alex antennas too; 4 with the HL2
  I/O board too: the `ioBoard` object, `setAlexRxAntenna` (which needs 3)
  and the filter policy command `setAlexBpfMode` (which needs 4); 5 (group
  B fix wave) with `rxOutOnTx` on `alexAntennas` two-way (RX bypass on TX,
  the VFO flag's BYPS; the Core applies it through its AlexController,
  which clears `ext1OutOnTx` and `ext2OutOnTx`); 6 (parity Task 12) with
  the rest of the transmit half of `alexAntennas` two-way: `txAntennas`,
  `blockTxAnt2`, `blockTxAnt3`, `ext1OutOnTx`, `ext2OutOnTx` and
  `rxOutOverride`, each applied through the Core's AlexController as the
  local Antenna Control tab applies it (a TX antenna on a port blocked for
  transmit is kept and settles with a reason; Block TX moves a band on that
  port back to Ant 1; Ext 1 or Ext 2 on TX clears the other two). These
  seven writes have no on-air rule, on the Core or in a local window, as in
  Thetis: an antenna change while the radio transmits goes out on the TX
  routing, and a relay flag reaches the relays at the next MOX edge. A
  station no longer sends 3, 4 or 5; a client compares the version as a
  minimum (section 6.2), so 6 serves `setAlexRxAntenna`, `setAlexBpfMode`
  and `rxOutOnTx` too. 6 also carries `setAlexTxAntenna` (`band` i64,
  `antenna` i64; parity mini-round): one band's TX antenna, applied
  through the Core's AlexController as the local grid applies it, so an
  edit cannot put back a TX antenna the Core changed on another band in
  between, which a whole `txAntennas` list built before that change does.
  A client at 6 sends one band's TX antenna with it; the Core still takes
  a whole `txAntennas` list from a window that sends one. A port blocked for transmit is refused with "An
  antenna blocked for transmit cannot be a band's TX antenna.", an antenna
  outside 1 to 3 with "Antennas are numbered 1 to 3."; like the list, it
  has no on-air rule. 7 (parity Task 14) adds HL2 Options' I2C Control
  tool and Pin Control through the Core: `requestIoBoardI2c` and
  `setIoBoardOutput` (section 9.1), the board's output pins as `outputs`
  on `ioBoard`, and the Alex tab's three transmit high-pass switches
  (HPF Bypass on TX, HPF Bypass on PureSignal, Disable 6m LNA on TX)
  taken from a window, on and off the air (section 8.1). A station no
  longer sends 6; 7 serves every earlier version's command and property.
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
  `remoteTgxlControlVersion` is followed by `stationIdentityVersion`.
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
  properties, which keep their ordinals. At 3 (group B fix wave) it also
  carries the RF-Kit's average response time (`rfkitRttAvgMs`), appended
  the same way.

- `stationIdentityVersion`: sent only at agreed minor 11. 1 when the
  Core has its identity key and signs devices in by key (section 3.5);
  0 when that key is unusable. A client learns the same before
  capabilities from the hello's `features.deviceAuth`.
- `deviceAdminVersion`: sent only at agreed minor 11. 1 when the
  Core's identity key is usable (the same condition as
  `stationIdentityVersion`): the `devices` object (section 7.1) and
  `devices.revoke`, `station.rename`, `station.acknowledgeKeyBackup` and
  `station.retireToken` (section 9.1), for a peer whose hello declares
  `deviceAuth`. 0 otherwise.
- `pairingVersion`: sent only at agreed minor 11, last. 1 when the Core
  pairs devices (the hello's `features.pairing`, section 3.6): the
  `devices` object's `pairingWindowOpen` and `pairingCode`, and
  `pairing.open` and `pairing.close` (section 9.1), for the same peers as
  `deviceAdminVersion`. 0 otherwise.
- `stationCatalogVersion`: sent only at agreed minor 11. 1 on every
  Core that has it: the read-only `catalog` object (section 7.4) goes to
  every peer at minor 11. A Core from before it sends neither the entry
  nor the object.
- `displayExtrasVersion`: sent only at agreed minor 11. 2 while the
  Core's media is enabled. At 1 a `subscribe` operation (section 11) may
  carry the display extras fields, and the Core sends an NSDX datagram
  beside each NSDC frame of an endpoint that asks for a section
  ([display extras v1](2026-09-23-display-extras-v1.md)); 2 adds the
  media control operation `clarity-retune`, Clarity's Re-tune for one
  endpoint ([remote media control
  v1](2026-09-20-remote-media-control-v1.md), "Clarity re-tune"). The
  extras need 1, so a client that compares the version as a minimum reads
  2 as it read 1. 0 otherwise; a Core from before it sends no entry and
  refuses the fields as keys it cannot read.
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
  refused while the radio is on the air (section 7.3). At 8 it also covers
  Setup > Hardware Config's OC Outputs and Calibration: the OC transmit
  pins (`hardware/<mac>/oc/tx/...`: the HF and SWL TX matrices and their
  resets), taken while the radio is off the air; and the OC pin actions
  (`hardware/<mac>/oc/actions/...`), TX Display Cal and Volts/Amps
  Calibration (`hardware/<mac>/cal/txDisplayOffset`, `cal/paSens`,
  `cal/paOffset` and the Calibration tab's copies under
  `hardware/<mac>/paCalibration/cal/`), taken on and off the air, as Thetis
  changes them while transmitting (section 8). The Core applies each to its
  OC matrix or calibration at once; an OC matrix change, and the N2ADR
  switch on its HL2 (which now applies its whole preset, transmit pins
  included), wait until the radio is back on receive. The keying set stays refused on a receive-only
  Core, on and off the air, and so do raw settings writes of
  `hardware/<mac>/tx/...`, `powerByBand` and `tunePowerByBand` (the
  `transmit` object owns them). A window whose Core sends 0 keeps its
  transmit settings unavailable. A peer below agreed minor 11 is never
  offered it, and a receive-only Core refuses its transmit writes and DSP >
  Options TX keys as before. `transmitSettingsVersion` is followed by
  `bandSelectVersion`.
- `bandSelectVersion`: sent only at agreed minor 11, and 0 on a
  station with no radio model. At 1 the Core takes `slice.selectBand`
  (section 9.1), a device's band button for a slice, for the bands the
  catalogue's `bands` lists (section 7.4). An app keeps its band buttons
  greyed on a Core that sends 0 or no entry. It is followed by
  `meterReadingsVersion`.
- `meterReadingsVersion`: sent only at agreed minor 11, and 0 on a
  station whose radio model runs no meter pump (no radio model, or a
  window's own). At 1 each slice carries the Core's ADC and AGC readings
  (`adcPeakDbfs`, `adcAverageDbfs`, `agcGainDb`, `agcPeakDb`,
  `agcAverageDb`, section 7.1), refreshed at the Core's meter pump rate as
  `signalPeakDbm` is, and the Multimeter polling delay (`MultimeterDelayMs`,
  section 8) sets that rate at once when a window writes it. A window's ADC
  Peak, ADC Average, AGC Gain, AGC Peak and AGC Average meters read them;
  on a Core that sends 0 or no entry those meters show no reading, never a
  frozen value. It is followed by `dspInfoVersion`.
- `dspInfoVersion`: sent only at agreed minor 11, last, and 0 on a station
  with no radio model of its own (a window's). At 1 the Core says how
  long its last DSP Options apply took, as `radio`'s
  `dspOptionsLastApplyMs`, and each slice's receiver's narrowest notch, as
  the slice's `minNotchWidthHz` (section 7.1), and it takes
  `dsp.filterResponse`, the filter graph's curve (section 9.1). Which
  noise reduction the Core runs is `dspAssetVersion`'s (3 and 4 above).
  On a Core that sends 0 or no entry a window shows the high-resolution
  filter graph box disabled with "This Core does not send its filter
  curve. Updating the Core may help.".

- `sessionHolderVersion`: sent only at agreed minor 11, last, and only to
  a peer whose hello declared `sessionHolder` 1 with `deviceAuth` 1; any
  other peer is sent no entry (and reads 0), so its capabilities are
  today's. 1: up to four devices at once, the `connectedDevices` object
  (section 7.1), `session.leave` (section 9.1), and sharing the radio's
  receivers: `confirm.request` and `notice`, and the verbs
  `confirm.proceed`, `confirm.cancel` and `notice.takeBack` (section 7.5).
  The table above shows the value a declaring peer is sent.
- While several devices are on a Core, media and telemetry go to one of
  them (section 11): any other is sent `remoteMediaVersion`,
  `remoteWidebandDisplayVersion`, `remoteAudioStatusVersion`,
  `spectrumGrantVersion`, `audioProfileVersion`, `audioClockVersion`,
  `receiverAudioVersion`, `headphonesMixVersion`, `psDisplayVersion`,
  `displayExtrasVersion` and `stationTelemetryVersion` as 0 and no display
  budget. A Core with one device on it sends that device what the table
  says.

- `remoteTxVersion`: sent only at agreed minor 11, last, and only to a
  peer whose hello declared `remoteTx` 1; any other peer is sent no entry
  (and reads 0). 1: `txPermitted` is the station transmit gate's answer
  for that session, `tx.setTxSlice` and the keying verbs `tx.key`,
  `tx.unkey`, `tx.tune` and `tx.twoTone` (section 9.1), and the refusals
  of section 18. 2 (iPhone app plan Task 77): taking transmit, `tx.take`
  (with `sessionHolderVersion` 1), its `takeTransmit` question and
  `transmitTaken` notice (sections 7.5 and 18.9), the radio's own PTT
  taking transmit, the transmitter's settings held by the holder, and
  `tx.tunerTune`, the Tuner Genius autotune (section 18.6). A client sends
  `tx.take` or `tx.tunerTune` only to a Core that sent 2 or more. The table
  above shows the value a declaring peer is sent.
- `txRefusalCode`, `txRefusalReason`, `txRefusalFix` (utf8, desktop remote
  transmit): sent right after `remoteTxVersion` and only with it. While
  `txPermitted` is false they are the gate's refusal for that session
  (section 18.3: its code, its sentence as the operator reads it, and its
  fix or empty); while it is true all three are empty. A client shows the
  sentence on its disabled transmit controls as sent.
- `txStateVersion` (iPhone app plan Task 39): sent after `remoteTxVersion`
  and its three `txRefusal` entries, and only with them. 1: the Core sends
  the read-only `txState` object (section 18.8) to that peer; any other
  peer never sees it or its schema.

`txPermitted` (iPhone app plan Task 34) is true only for a session the
station transmit gate permits (section 18.1): false until
`snapshot.complete` has been sent to it, false for a peer whose hello did
not declare `remoteTx`, false for every peer while the Core's
`remote_transmit` is deny, and false while another device holds transmit.
A session learns a change from a new `capabilities` message (section 5.2);
nothing else is sent again. A peer that declared `remoteTx` is also sent a
new `capabilities` when only the refusal changes (for example from
"Transmit is changing hands." to "<holder> has the transmitter."). An older window that reads the flag without
declaring `remoteTx` therefore never sees it true.

### 6.4 The capabilities message

`capabilities` carries property entries (section 4.1) in the order below.
The display budget entries (`displayApplicationBytesPerSecond`,
`spectrumSampleUnitsPerSecond`, `displayBudgetGeneration`,
`remotePs3DisplaySubscribed`, `displayBudgetReason`) are present only with a
usable budget, and `displayBudgetReason` only at agreed minor 11. The radio
identity entries from `hpsdrModel` onwards are present only at agreed minor
11, and `sessionHolderVersion`, last, only for a peer that declared
`sessionHolder` (section 6.1); `remoteTxVersion` and the three
`txRefusal` entries after it only for a peer that declared `remoteTx`. A client ignores a capability it does not know
(`StationCapabilities::fromUpdates`).

**Each device's share of the display budget** (iPhone app plan Task 76; the
several-devices design, ruling 9.3 and design ruling 9.3a). The Core has one
display budget, its total: the display allowance its configuration sets, or
its computed ceiling, lowered by the load governor when the Core computer is
short of processing time. Every admitted session with media is given its own
share of that total (`DisplayBudgetSplit`), and its capabilities carry that
share in the budget entries: `displayApplicationBytesPerSecond` and
`spectrumSampleUnitsPerSecond` are the device's own, and
`displayBudgetGeneration` is the device's own generation. A share that does
not change keeps its generation; one that does takes the total's generation
when that is newer, otherwise the device's last plus one, so a device alone
on the Core sees exactly the generations it saw before shares existed. When
a device is admitted or leaves, the total changes, or a device's displays
ask for more or less, every device whose share or reason changed is sent
`capabilities` again. The rules of the
split: the PureSignal display's charge comes off the total once and belongs
to the device that subscribed to it (`remotePs3DisplaySubscribed` is true
for that device only); a network device holding transmit gets its whole
request, the rest is shared among the others; with transmit unheld, held by
the station device or held by a device that is away, every device gets an
equal share, and a device asking for less leaves the difference to the
rest (max-min fair). A device's request is what its displays ask for: the
sum of its display subscriptions' charges at the frame rate it subscribed
at and the pixels it subscribed at, clamped to what its window can carry
(the receiver's bins in that window), before the budget clamps them, a
subscription refused for the
budget included, until the display is closed, asked for again, or 10 s
pass after its refusal without either (the media control document's
display budget section), and never
less than one useful pan (256 pixels at 10 frames a second with its wide
plane). What that guarantees: with no network device holding transmit
while present, each device's share is at least the smaller of one useful
pan and an equal part of the total; the Core's own cut for a busy computer
never takes the total below PureSignal's display plus one useful pan per
device, so under that cut each device keeps one pan. Only a display
allowance configured below one pan per device can leave each device less.
Beside a device holding transmit, the others share only what its request
leaves, which can be nothing useful (a share of 1) while it asks for the
whole total. What no device asks for is shared equally among them as room to grow,
so a device alone has the whole total. A subscription is admitted against the share the
device has once it asks for it (for the transmit holder, its whole
request), not the share it had. **Transmit joins here:** until the transmit
holder exists (Task 34) transmit counts as unheld; Task 34 names the holder.
The Core hands each device a share, never a frame rate: each client plans
its own displays inside its share, and a share too small for one pan at 256
pixels and 10 frames a second suspends that device's display, pane and
slice kept. Every client, holding transmit or not, first subscribes its
displays at the pixels and frame rate the operator wants, since its request
is what gives it its share. A subscription refused because it does not fit
(reason "The Core's display limit has no room left.") is the answer: the
`capabilities` with the device's new share arrive before the refusal, and
the client plans inside that share and subscribes again. It asks for what it
wants again when that grows (a pane added, widened or sped up; a pane
widened by a resize asks once its width has held for 200 ms, not at every
step of the resize) and when the
transmit holder changes (`txState`'s `holderEpoch` or `holderAway` moves: it
takes transmit, the holder changes, or a holder lets go or goes away), never
merely because a new generation arrived. Audio is never split and never cut
when the Core runs short.

`displayBudgetReason` says which of the Core's limits is short:

| Value | Meaning |
| --- | --- |
| `none` | the device's share is not below its request, and nothing is cut |
| `coreBusy` | the governor has cut the total, and the device is alone on the Core (or its share still covers its request) |
| `sharedConnection` | another device is admitted and this device's share is below its request; the governor has cut nothing (the devices share what the Core sends) |
| `sharedProcessing` | another device is admitted and this device's share is below its request; the governor's cut is in force (the Core computer is short of processing time) |

A device's request is its demand as above (what its displays ask for, at
least one useful pan). Because every client first asks for what the
operator wants, a device that wants more than its share while another
device is admitted hears `sharedConnection` or `sharedProcessing`; one
whose share covers all it asks for hears `none` or `coreBusy`.

`sharedConnection` and `sharedProcessing` go only to a device that declared
`sessionHolder` (section 6.1). Any other device is told `none` in place of
`sharedConnection` and `coreBusy` in place of `sharedProcessing`, so an
older window sees only the values it was built for.

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
| 48 | `stationIdentityVersion` | `i64` |
| 49 | `deviceAdminVersion` | `i64` |
| 50 | `pairingVersion` | `i64` |
| 51 | `stationCatalogVersion` | `i64` |
| 52 | `displayExtrasVersion` | `i64` |
| 53 | `transmitSettingsVersion` | `i64` |
| 54 | `bandSelectVersion` | `i64` |
| 55 | `meterReadingsVersion` | `i64` |
| 56 | `dspInfoVersion` | `i64` |
| 57 | `sessionHolderVersion` | `i64` |
| 58 | `remoteTxVersion` | `i64` |
| 59 | `txRefusalCode` | `utf8` |
| 60 | `txRefusalReason` | `utf8` |
| 61 | `txRefusalFix` | `utf8` |
| 62 | `txStateVersion` | `i64` |

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

**AccessoryDataModel** (48 properties)

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
| 47 | `rfkitRttAvgMs` | `i64` | outbound |  |

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
| 3 | `txAntennas` | `utf8` | bidirectional |  |
| 4 | `blockTxAnt2` | `bool` | bidirectional |  |
| 5 | `blockTxAnt3` | `bool` | bidirectional |  |
| 6 | `rxOutOnTx` | `bool` | bidirectional |  |
| 7 | `ext1OutOnTx` | `bool` | bidirectional |  |
| 8 | `ext2OutOnTx` | `bool` | bidirectional |  |
| 9 | `rxOutOverride` | `bool` | bidirectional |  |

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

**ConnectedDevicesFacade** (3 properties)

| Ordinal | Property | Wire kind | Direction | Enum values |
| --- | --- | --- | --- | --- |
| 0 | `listJson` | `utf8` | outbound |  |
| 1 | `revision` | `i64` | outbound |  |
| 2 | `deviceLimit` | `i64` | outbound |  |

**DspAssetService** (12 properties)

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
| 8 | `dfnrModelStatus` | `utf8` | outbound |  |
| 9 | `dfnrRunnable` | `bool` | outbound |  |
| 10 | `mnrStatus` | `utf8` | outbound |  |
| 11 | `mnrRunnable` | `bool` | outbound |  |

**IoBoardHl2Facade** (4 properties)

| Ordinal | Property | Wire kind | Direction | Enum values |
| --- | --- | --- | --- | --- |
| 0 | `detected` | `bool` | outbound |  |
| 1 | `hardwareVersion` | `i64` | outbound |  |
| 2 | `registers` | `utf8` | outbound |  |
| 3 | `outputs` | `i64` | outbound |  |

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

**RadioModel** (25 properties)

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
| 21 | `bandOutputsByte` | `i64` | outbound |  |
| 22 | `bandOutputsBand` | `i64` | outbound |  |
| 23 | `bandOutputsKeyed` | `bool` | outbound |  |
| 24 | `dspOptionsLastApplyMs` | `i64` | outbound |  |

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

**SliceMarker** (14 properties)

| Ordinal | Property | Wire kind | Direction | Enum values |
| --- | --- | --- | --- | --- |
| 0 | `sliceId` | `i64` | constantSnapshot |  |
| 1 | `ownerDeviceId` | `utf8` | outbound |  |
| 2 | `ownerName` | `utf8` | outbound |  |
| 3 | `ownerShortName` | `utf8` | outbound |  |
| 4 | `ownerKind` | `utf8` | outbound |  |
| 5 | `ownerAway` | `bool` | outbound |  |
| 6 | `frequency` | `f64` | outbound |  |
| 7 | `dspMode` | `enum` | outbound | 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13 |
| 8 | `filterLow` | `i64` | outbound |  |
| 9 | `filterHigh` | `i64` | outbound |  |
| 10 | `txSlice` | `bool` | outbound |  |
| 11 | `band` | `enum` | outbound | 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26 |
| 12 | `streamIndex` | `i64` | outbound |  |
| 13 | `psPaused` | `bool` | outbound |  |

**SliceModel** (150 properties)

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
| 144 | `adcPeakDbfs` | `f64` | outbound |  |
| 145 | `adcAverageDbfs` | `f64` | outbound |  |
| 146 | `agcGainDb` | `f64` | outbound |  |
| 147 | `agcPeakDb` | `f64` | outbound |  |
| 148 | `agcAverageDb` | `f64` | outbound |  |
| 149 | `minNotchWidthHz` | `f64` | outbound |  |

**StationCatalog** (2 properties)

| Ordinal | Property | Wire kind | Direction | Enum values |
| --- | --- | --- | --- | --- |
| 0 | `json` | `utf8` | outbound |  |
| 1 | `revision` | `i64` | outbound |  |

**StationDevicesFacade** (9 properties)

| Ordinal | Property | Wire kind | Direction | Enum values |
| --- | --- | --- | --- | --- |
| 0 | `listJson` | `utf8` | outbound |  |
| 1 | `revision` | `i64` | outbound |  |
| 2 | `stationLabel` | `utf8` | outbound |  |
| 3 | `claimed` | `bool` | outbound |  |
| 4 | `tokenActive` | `bool` | outbound |  |
| 5 | `keyBackupAcknowledged` | `bool` | outbound |  |
| 6 | `keyPath` | `utf8` | outbound |  |
| 7 | `pairingWindowOpen` | `bool` | outbound |  |
| 8 | `pairingCode` | `utf8` | outbound |  |

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

**TransmitModel** (86 properties)

| Ordinal | Property | Wire kind | Direction | Enum values |
| --- | --- | --- | --- | --- |
| 0 | `mox` | `bool` | outbound |  |
| 1 | `tune` | `bool` | outbound |  |
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
| 85 | `voxEnabled` | `bool` | bidirectional |  |

**TransmitState** (29 properties)

| Ordinal | Property | Wire kind | Direction | Enum values |
| --- | --- | --- | --- | --- |
| 0 | `keyed` | `bool` | outbound |  |
| 1 | `tuning` | `bool` | outbound |  |
| 2 | `twoTone` | `bool` | outbound |  |
| 3 | `txSliceId` | `i64` | outbound |  |
| 4 | `keyedByName` | `utf8` | outbound |  |
| 5 | `keyedByKind` | `utf8` | outbound |  |
| 6 | `keyedTrigger` | `utf8` | outbound |  |
| 7 | `keyedSinceMs` | `i64` | outbound |  |
| 8 | `timeOutRemainingSeconds` | `i64` | outbound |  |
| 9 | `forwardPowerWatts` | `f64` | outbound |  |
| 10 | `reflectedPowerWatts` | `f64` | outbound |  |
| 11 | `swr` | `f64` | outbound |  |
| 12 | `alcDb` | `f64` | outbound |  |
| 13 | `micLevelDb` | `f64` | outbound |  |
| 14 | `txEnding` | `bool` | outbound |  |
| 15 | `stopReason` | `utf8` | outbound |  |
| 16 | `stopText` | `utf8` | outbound |  |
| 17 | `stopSerial` | `i64` | outbound |  |
| 18 | `holderDeviceId` | `utf8` | outbound |  |
| 19 | `holderName` | `utf8` | outbound |  |
| 20 | `holderShortName` | `utf8` | outbound |  |
| 21 | `holderKind` | `utf8` | outbound |  |
| 22 | `holderSource` | `utf8` | outbound |  |
| 23 | `holderForSeconds` | `i64` | outbound |  |
| 24 | `holderEpoch` | `i64` | outbound |  |
| 25 | `holderAway` | `bool` | outbound |  |
| 26 | `holderTransferring` | `bool` | outbound |  |
| 27 | `keyedForSeconds` | `i64` | outbound |  |
| 28 | `stopEpoch` | `i64` | outbound |  |

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
| `devices` | `StationDevicesFacade` |
| `connectedDevices` | `ConnectedDevicesFacade` |
| `txState` | `TransmitState` |
| `catalog` | `StationCatalog` |
| `pan:<i>` | `PanadapterModel` |
| `slice:<id>` | `SliceModel` |
| `marker:<id>` | `SliceMarker` |

<!-- /surface -->

Notes on the keys:

- **`pan:<i>`.** The station watches every panadapter its model holds, but
  no code path in `nereusd` adds one: panadapters live in the window, and
  the Core's spectrum travels on display endpoints (media control). A real
  Core therefore sends no `pan:<i>` objects, and a write to one changes
  nothing; the key stays unused (the several-devices design, ruling 5.12).
  A device's pans are its own: `addSliceOnPan`'s `panId` is the device's
  own key, so two devices' "pan-0" are two pans. The surface records one
  so the key pattern is known.
- **Minor 11 objects.** `stepAtt`, `alexAntennas` and `ioBoard` are sent
  only to a peer at agreed minor 11 and only while `radioHardwareVersion` is
  at least 1, 2 and 3 respectively. `amplifier` and `rfkit` are sent only at
  minor 11 on a Core that owns its accessories, `accessoryData` only at
  minor 11 while `accessoryDataVersion` is at least 1, `accessorySettings` only at
  minor 11 while `remotePgxlControlVersion` is at least 3 or
  `remoteTgxlControlVersion` at least 1, `stationTci` only at
  minor 11 on a Core that runs a station TCI server, and `catalog` only at
  minor 11 while `stationCatalogVersion` is at least 1
  (`StationServer::sendToSession`). An older peer never sees their schema
  either.
- **`ioBoard` `outputs`.** The HL2 I/O board's output pins, one bit per
  output (o0 in bit 0), as the Core last read them back from the board's
  output register (169 at 0x1d on I2C bus 1): after `setIoBoardOutput`,
  after a `requestIoBoardI2c` write or read of that register, and when a
  window opens HL2 Options (mi0bot reads it back on the tab's Enter and
  after each pin click). 0 until the Core has read it. `outbound`, from
  `radioHardwareVersion` 7 (parity Task 14); HL2 Options' output strip
  shows it in both windows.
- **`slice:<id>` TX mark.** `txSlice` (bool, outbound) is true only while
  the slice is the transmit slice and its owner is
  `txState.holderDeviceId` (the several-devices design, ruling 5.4a;
  iPhone app plan Task 77), and false otherwise, whatever the Core binds:
  with transmit unheld no slice shows TX, and a slice the radio's own PTT
  transmits on shows none (its owner reads that from `txState`: `keyed`,
  `holderSource` `radioPtt`, `txSliceId` its own slice). A change of
  holder sends the changed values, on `slice:` and `marker:` alike.
- **`slice:<id>` minimum notch width.** `minNotchWidthHz` (f64, outbound,
  no WRITE; parity Task 16, `dspInfoVersion` 1) is the narrowest notch the
  slice's receiver can make, in Hz, as the Core's own TNF page reads it
  (WDSP's `RXANBPGetMinNotchWidth`, which moves with the channel's filter
  size and rate), 0 while the slice has no receiver. A window's TNF page
  shows it, and its pans check the notch width presets against it and
  draw the notch dent with it.
- **`slice:<id>` ADC and AGC readings.** `adcPeakDbfs`, `adcAverageDbfs`,
  `agcGainDb`, `agcPeakDb` and `agcAverageDb` (f64, outbound, no WRITE;
  parity Task 15) are the Core's receive meters for the slice's receiver,
  the ones a container meter binds to ADC Peak, ADC Average, AGC Gain, AGC
  Peak and AGC Average. The Core's meter pump reads them from WDSP each
  tick as Thetis's CalculateRXMeter does (`RXA_ADC_PK`, `RXA_ADC_AV`,
  `RXA_AGC_GAIN`, `RXA_AGC_PK`, `RXA_AGC_AV`), with no calibration offset;
  AGC Gain is Thetis's reading, 0 minus `RXA_AGC_GAIN`, which a local window
  shows too. -400 is no reading: before the radio link is up, with no
  receiver for the slice, and before WDSP has measured a block. While the
  radio is on the air the pump leaves them where they were. They reach a
  window whatever `meterReadingsVersion` says; a window reads them only when
  it is at least 1 (section 6.3). A window never writes them back.
- **`devices`.** Sent only to a peer at agreed minor 11 whose hello
  declares `deviceAuth` 1 or later, while `deviceAdminVersion` is 1
  (`StationServer::sendToSession`). A window that declares nothing (today's
  desktop) and an older peer never see it or its schema. Every property is
  `outbound`; the object changes only through its six commands (section
  9.1), and a write to it is refused as any `outbound` write is. Its
  properties (`StationDevicesFacade`):
  - `listJson` (`utf8`): a JSON array of the paired devices in pairing
    order, each `{id, name, shortName, kind, pairedAt, lastSeen,
    connected}`: `id` is the device's key fingerprint (SHA-256 of its
    public key's DER) in base64url, `kind` is `phone`, `tablet` or
    `computer`, the two times are ISO 8601 UTC (`""` when never seen), and
    `connected` says whether that device holds a session now. `name` and
    `shortName` are numbered as `connectedDevices` numbers them (below), so
    one device reads the same on both lists; `shortName` is the one the
    device last signed in with (section 3.5), or its kind's word ("Phone",
    "Tablet", "Computer") when it sent none usable.
  - `revision` (`i64`): moves by one with every change to the object, from
    0 to 2^32 - 1 and then round to 0; compare by serial-number
    arithmetic.
  - `stationLabel` (`utf8`): the Core's label as displayed (section 8.2's
    `StationLabel`, or `StationCallsign` until the first rename; `""`
    when neither gives one).
  - `claimed` (`bool`): a device is paired, or the token is active
    (`DeviceStore::isClaimed`).
  - `tokenActive` (`bool`): the old pairing token still signs in (section
    3.3).
  - `keyBackupAcknowledged` (`bool`): the operator confirmed a backup of
    this Core's identity key (`station.acknowledgeKeyBackup`); false on a
    new Core and again after the key is replaced.
  - `keyPath` (`utf8`): where the identity key file is on the Core.
  - `pairingWindowOpen` (`bool`, `pairingVersion` 1): the pairing window
    is open (section 3.6).
  - `pairingCode` (`utf8`, `pairingVersion` 1): the current pairing code,
    `""` while the window is closed and while no code is shown. Sent only
    to a connection signed in with a paired device's own key; any other
    connection receives `""` (`StationServer::withPairingCodeFor`).
- **`connectedDevices`** (iPhone app plan Task 71;
  `ConnectedDevicesFacade`): who is on the Core, the list a device's
  Devices page reads for "Connected now". Sent only at agreed minor 11 to
  a peer whose hello declared `sessionHolder` 1 with `deviceAuth` 1
  (`sessionHolderVersion` 1, section 6.3); any other peer never sees it or
  its schema. Every property is `outbound`:
  - `listJson` (`utf8`): a JSON array, one entry per device that holds a
    place, live or away, in the order they were let in:
    `{deviceId, name, shortName, kind, paired, hostsCore, revocable, state,
    holdsTransmit, lastActivitySeconds, connectedForSeconds,
    awayForSeconds, transmittingForSeconds, listeningOn}`. `deviceId` is
    the `devices` object's `id` for a paired device, and `token:<n>` for a
    window signed in with the older token and no key (`paired` false),
    named "Computer at <its address>" with the short name "Computer".
    `kind` is `phone`, `tablet` or `computer`, or `station` for a hosting
    desktop's own window (`hostsCore` true). `revocable` is false for a
    hosting desktop's window and for a token window. `state` is
    `listening`, or `away` for a device whose link dropped without leaving
    and that still holds its place (section 12.4). `holdsTransmit` is true
    for the device that holds transmit, keyed or not; `state` is
    `transmitting` and `transmittingForSeconds` how long while it is on the
    air (section 18.2). `transmittingOn` (iPhone app plan Task 77) is its
    transmit slice, `{sliceId, letter, band, mode}`, while it is on the
    air, and absent otherwise. `listeningOn` (iPhone app plan Task 73) lists every slice
    the device owns, an away device's included, each `{sliceId, letter,
    band, mode}` (`letter` "A" for slice 0; `band` and `mode` the values
    the slice's own `band` and `dspMode` carry); a slice's frequency is on
    its `marker:<id>`, so tuning does not change the list. Slices the Core
    holds for a device that has left show only on their markers.
    `lastActivitySeconds` counts from the device's last command, property
    write or settings write, never a heartbeat, and moves at most once a
    minute; `connectedForSeconds` from when the device took its place;
    `awayForSeconds` from when it went away, 0 while it is not.
  - `revision` (`i64`): moves by one with every change to the list, from 0
    to 2^32 - 1 and then round to 0; compare by serial-number arithmetic.
    The list changes, and is sent again, only when something in it other
    than time passing changes.
  - `deviceLimit` (`i64`): 4.

  **Names.** A device's name is the one it paired with, or the one the
  Core gives a token window; its short name is the one it signs in with
  (section 3.5), or its kind's word. When two devices carry the same name,
  the one paired later gets the next free number ("iPhone", "iPhone 2");
  short names are numbered on their own collisions the same way ("Phone",
  "Phone 2"). The order is the paired devices in pairing order, then a
  hosting desktop the Core has not paired, then token windows in the order
  they connected. Names and short names are the operator's own words: the
  Core checks them as names (section 3.5), never against its own wording
  rules.

  **The one clock convention.** Every time the Core sends about a session,
  a device, transmit, a notice or an end is a duration in whole seconds,
  measured on the Core's own monotonic clock when the message is encoded,
  named `...ForSeconds`, `...Seconds` or `secondsAgo`. No wall-clock time
  from the Core reaches these screens, so a Core whose clock is wrong still
  counts right. A duration inside an object is measured again whenever
  that object or property is sent (its `object.create`, a `delta` on any
  change), and not otherwise: an app counts on from its own receipt.
  `devices`' `pairedAt` and `lastSeen` stay ISO 8601 dates, since they
  outlive a session and a restart.
- **`txState`** (iPhone app plan Task 39; `TransmitState`,
  `txStateVersion` 1): the Core's transmitter, its meters and why it last
  stopped a transmission, section 18.8. Sent only at agreed minor 11 to a
  peer whose hello declared `remoteTx` 1; every property is `outbound`.
- **`catalog`.** The values the Core owns and an app draws its controls
  from (section 7.4). Both properties are `outbound`
  (`StationCatalog`): `json` (`utf8`), the catalogue, and `revision`
  (`i64`), which moves by one each time `json` changes, from 0 to 2^32 - 1
  and then round to 0; compare by serial-number arithmetic. A write to
  either is refused as any `outbound` write is. Today's desktop window
  holds no object for the key and drops it, as it does any class it does
  not know.
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
- **`radio` DSP facts** (parity Task 16, `dspInfoVersion` 1). Outbound,
  no WRITE. Which noise reduction the Core runs is not here: it is
  `DspAssetService`'s `dfnrRunnable` and `mnrRunnable` (section 6.3).
  `dspOptionsLastApplyMs` (`i64`) is how long the Core's last DSP Options
  apply took (a channel rebuild after a buffer size, filter size, filter
  type or sample rate change), in milliseconds, 0 before any; a window's
  Setup > DSP > Options "Time to last change" shows it. It reaches a window
  whatever `dspInfoVersion` says; a window reads it only when it is at
  least 1 (section 6.3).
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
- **Whose each slice is** (iPhone app plan Task 73; the several-devices
  design, rulings 5.1 to 5.6). With several devices on one Core every
  slice has an owner: the device that made it, the device that adopted it
  (the first device let in while no other is on the Core takes every slice
  nobody owns, and a device alone on the Core takes a slice the Core makes
  itself, such as when its radio arrives late), or the station itself, which runs a slice **held for** a
  device that has left while no other device was on the Core, until that
  device signs in again. A session receives its own slices as `slice:<id>`
  objects and nothing else of any other slice. A device let in that owns
  no slice gets one at once, at the frequency of the Core's current
  slice and on its receiver; with every slice in use it starts with none.
  Owners live at the Core: no `slice:` property changed. Slice letters
  come from one pool (`slice:<id>` is letter 'A' + id on every device), a
  slice keeps its letter for its whole life, and a slice restored for a
  device takes its old letter when it is free.
- **`marker:<id>`** (`SliceMarker`, `sessionHolderVersion` 1). One per
  slice, sent to every session with the feature except the one whose
  slice it is (for a held slice, the device it is held for); an older
  window never receives one or its schema. Every property is `outbound`:
  `sliceId` (`constantSnapshot`; its letter is 'A' + `sliceId`),
  `ownerDeviceId` (the owner's id as `connectedDevices` names it, or the
  id of the device it is held for; `""` for a slice nobody owns),
  `ownerName` and `ownerShortName` (numbered as below), `ownerKind`
  (`phone`, `tablet`, `computer`, or `station` for a slice nobody owns),
  `ownerAway` (the owner is away, or the slice is held for it), and
  `frequency`, `dspMode`, `filterLow`, `filterHigh`, `txSlice`, `band`,
  `streamIndex` (the receiver it sits on, -1 when none; a screen shows
  "Receiver `streamIndex` + 1") and `psPaused`, each as the slice's own.
  `txSlice` is true only while the slice is the transmit slice and its
  owner is `txState.holderDeviceId` (the several-devices design, ruling
  5.4a; iPhone app plan Task 77), as on a `slice:` object. A marker has no colour on the wire: a client draws it in
  its letter's colour. A write to a marker is refused (section 7.3).
- **A change of owner** (a device adopting slices nobody owned, a slice
  passing to the station for a device that left, a held slice returning)
  reaches each session as `object.destroy` of the form it had and
  `object.create` of the form it has now (`slice:<id>` to `marker:<id>`,
  or back). A session first sent an object of a class after its
  connect-time burst (its first marker, when a second device arrives) is
  sent that class's `schema` just before it.
- **Unknown classes.** A client that receives a schema for a class it does
  not know records the difference and drops that class's objects and
  deltas.
- **Band outputs (`radio`).** `bandOutputsByte`, `bandOutputsBand` and
  `bandOutputsKeyed` are the band-output (open collector) byte the Core's
  radio connection composed into the packet that carries it (Protocol 1
  bank 0, Protocol 2 high-priority byte 1401), the band index it was chosen
  for, and whether the transmitter was keyed. They are outbound only and
  change together (`RadioModel::bandOutputsChanged`). `bandOutputsBand` is
  -1 until the Core has composed a byte (no radio, or not yet connected),
  and the byte is then 0. A window shows these pins, never a byte of its
  own; a Core built before them never sends them, so a client shows no
  pins until all three have arrived with a band of 0 or more. No
  capability value gates them: an older client ignores the unknown
  properties (section 7.1's schema carries them), and a newer client
  reads the absence itself.

### 7.2 Deltas

The station collects property changes and sends them at most every 50 ms
(`kDefaultDeltaFlushMs`), one `delta` per object carrying the latest value
of each changed property. It collects them for each device's session
separately (`MirrorView`, iPhone app plan Task 72), so a device that
signs in never takes another device's pending changes with it. The desktop client collects its own writes on
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

`alexAntennas`' transmit properties (`txAntennas`, `blockTxAnt2`,
`blockTxAnt3`, `rxOutOnTx`, `ext1OutOnTx`, `ext2OutOnTx`, `rxOutOverride`,
two-way from `radioHardwareVersion` 5 and 6) are not held while the radio
is on the air, and a receive-only station takes them from any peer that
was offered the object: they key nothing, and Thetis changes them while
transmitting (section 6.3). A `txAntennas` list that asks for a port
blocked for transmit keeps that band's antenna and settles with "An
antenna blocked for transmit cannot be a band's TX antenna." A current
window changes one band's TX antenna with the `setAlexTxAntenna` command
instead (section 6.3), so its edit cannot put back another band the Core
changed; the whole-list write stays for a window that sends it.

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
requested properties back in that `delta`. Both go to the writer alone.

**Echo per writer** (iPhone app plan Task 72; the several-devices design,
ruling 5.7). What a write changes, the property written and any side effect
on another object (two slices on one receiver share its noise blanker, so
a change to one's blanker changes the other's), never comes back to the
writer as a `delta`: the writer has its `property.result` and the readback
above. Every other device's session receives each change as an ordinary
`delta`. With one device on the Core nothing on its wire changes. The
station's tests `tst_station_multi_session` and `tst_mirror_view` hold it
to this; the several-client fixtures of later tasks (`two-devices` and
after) carry it on the wire.

**Another device's slice** (iPhone app plan Task 73; the several-devices
design, ruling 5.9). A device changes only its own slices. A
`property.write` to a `slice:<id>` the writer does not own, or to any
`marker:<id>`, is refused before anything is read or applied: every
property answers not accepted, with no value, and the reason "That slice
belongs to <the owner's name>. It can be changed only there." (the name as
`connectedDevices` numbers it; "the Core" for a slice nobody owns). Nothing
else comes back. The commands `removeSlice`, `setActiveSliceById`,
`nnr.setDiagnostics`, `nnr.resetTuning`, `nnr.tryAgain`, `notch.add`,
`notch.addAtSlice`, `slice.selectBand`, `requestSliceSampleRate`,
`requestStreamCentre` and `requestStreamCtunPinned` naming a slice that is not the requester's
(another device's, one nobody owns, or one held for a device) are refused
with the same reason, whatever receivers are in use (section 9.1). On the
requester's own slice, a C-Tune centre change or pin and a slice's band
change follow the receiver rules of section 7.5, and a sample-rate change
the shared-setting rules of section 7.6.

**Waiting for you to confirm** (iPhone app plan Task 74; the
several-devices design, section 7.3). A write that would disturb another
device, from a device with `sessionHolderVersion` 1, is not applied. Its
`property.result` answers every property not accepted, with the value the
Core keeps and the reason "Waiting for you to confirm.", and a
`confirm.request` follows with `forWriteId` naming the write (section
7.5). Today that is a band change of the anchor's slice on a receiver
another device shares. A window without the feature is never asked: the
same write is refused with "This change would affect <names>. Update
NereusSDR to confirm changes that affect other devices.", the names as
`connectedDevices` numbers them.

A receiver property the station accepted reads back with its new value at
once: the station's receive DSP applies it afterwards, on its own thread,
never in the thread that answers the write (R-R3-39). So the result and
the `delta` show the value the station kept, not a value WDSP has already
reached. A change WDSP itself still refuses afterwards (a noise-reduction
model that is not loaded) comes back later as a `delta`: the slice's
noise-reduction status carries the reason, and a refused noise-reduction
selection returns to the one before it.

A property write never keys the transmitter: `transmit`'s `mox` and
`tune` travel from the Core only (outbound), and a write of either is
refused ("Use the transmit button."); a device keys with the keying verbs
(section 18.6),
and the transmit safety gates stay at the station (section 18). With
`remote_transmit` allow, the other `transmit` properties and the
transmit-side settings keys are written only by a session `txPermitted`
allows (refused otherwise with the gate's sentence); while another
device's holder is on the air, a change to the transmit path is refused
with the on-air sentence (section 18.4).

### 7.5 Receivers several devices share

iPhone app plan Task 74 (the several-devices design, sections 6.1 to 6.4,
7.3 and 7.4). Up to four devices share the radio's receivers. A slice
joins any receiver whose window covers its frequency, whoever claimed it,
as it always has; two devices' slices can therefore share one receiver.

**The anchor.** The device whose slice claimed a receiver anchors it.
When the anchor's last slice leaves the receiver, the anchor passes to the
device whose slice has been on it longest; nobody is asked or told. When
the last slice leaves, the receiver is free. A shared receiver's window
does not follow a slice's tuning inside it (the slice moves only its own
shift, as several slices on one receiver always have).

**The C-Tune pin** of a shared receiver is its anchor's.
`requestStreamCtunPinned` from another device is refused with "This
panadapter shows <anchor's name>'s receiver. Its C-Tune setting is
<anchor's name>'s.".
A pin lasts through its anchor's link dropping and its coming back as the
same device (its `streamCtunPinned` is as it left it); it ends when the
anchor leaves for good (`session.leave`, a token window's end, the end of
its 180 s, revocation).

**The anchor moves its panadapter.** A `requestStreamCentre` from the
anchor that would leave another device's slice outside the new window is
held: its `command.result` is not accepted, with the reason "Waiting for
you to confirm." and `values` holding `phase` `needsConfirmation`, and a
`confirm.request` of kind `panMove` follows. One that would leave the
anchor's own slice outside is refused as before. On proceed the window
moves; each other device's slice outside it moves to another receiver
(one whose window covers it, or a free one) or, with none free, closes,
and its device is told (`notice` `sliceMoved` or `sliceClosed`).

**The anchor changes band.** A `property.write` of the `frequency` of one
of the anchor's slices, outside its receiver's window, while another
device's slice shares the receiver, is the same question (`panMove`),
held as section 7.3 says, with `change` `{label, from, to}`:
"Receiver <n>" and the bands, "20 m" and "40 m". On proceed the receiver
follows the anchor's slice, centred on its new frequency; each other
device's slice the new window no longer covers moves or closes, as above;
one it still covers stays. When the anchor has another slice of its own
on the receiver that the new window would not cover, the change is not a
pan move: the slice leaves for another receiver as it always has (design
ruling 6.5a). Cancel changes nothing. With nobody else on the receiver,
nothing changes from before.

**A device that does not anchor moves its panadapter** by taking it,
with its slices there, to a free receiver centred where it asked; the
anchor is not disturbed and nobody is asked. Its slices must fit the new
window, as C-Tune requires.

**Taking a receiver** (the several-devices design, section 6.4). A
request that needs a receiver (`addSlice`, `addSliceOnPan`, a retune out
of a window, a panadapter move by a device that does not anchor) and is
refused because every receiver is in use is answered with the refusal,
whose words now end "The radio's receivers are in use by <names>.", and,
to a device with the feature, a `confirm.request` of kind `takeReceiver`
with one choice per receiver in use. On proceed with a choice the Core
checks again, closes every other device's slice on that receiver (even
one the new window would cover), tells each owner (`notice`
`receiverTaken`, with Take it back), and applies the held request on the
freed receiver. The taker's own slices there stay when the new window
covers them; a receiver where the taker's own slice would be left outside
is offered with `takeable` false and `why` "Your slice E would close.",
and a receiver the taker's own panadapter uses cannot give it a new one
(`takeable` false, "Your panadapter already uses this receiver."). When
the slice cap, not the receivers, is full, the question is `takeSlice`,
one choice per slice of another device, and taking one closes only that
slice (`notice` `sliceTaken`, with Take it back). A receiver or slice free
by the time of the answer is used without taking anything.

**Take it back.** `notice.takeBack {id}` asks the same question the other
way: a `takeReceiver` whose first choice is the receiver the taker now
holds, then any receiver free by then (for a slice, a `takeSlice` with the
taker's slice and, when one is free, a choice with `sliceId` -1). Its own
`command.result` is "Waiting for you to confirm." with `phase`
`needsConfirmation`. On proceed the Core closes what the choice names
(telling its owner, with Take it back) and recreates the device's closed
slices at their frequencies, modes and panadapters, with their settings.
Once taken back, a notice cannot be taken back again ("That can no longer
be taken back.").

**An older window** (a session without `sessionHolderVersion` 1) is never
asked: it gets the refusal only, naming the devices involved. When a take
closes its last slice its session ends: "<taker's name> took the receiver
this app was using. Update NereusSDR to share the Core.", not retryable,
`code` `takenOver`. With no slice for it at sign-in it is refused,
retryable: "All the radio's slices are in use. Try again when another
device closes one." when the slice cap is full, "All the radio's
receivers are in use. Try again when another device frees one." otherwise
(section 12.4).

**`confirm.request`** (Core to device, to a session with
`sessionHolderVersion` 1 only): `id` (number, unique on this Core), `kind`
(`panMove`, `takeReceiver`, `takeSlice`, `sharedSetting` (section 7.6),
`takeTransmit` (section 18.9)), `reason` ("Waiting for you to
confirm."), `affected` (array), `expiresInMs` (60000, `confirmExpiryMs` in
section 15), and, optionally, `change` `{label, from, to}` (absent for a
take), `choices` (a take of a receiver or a slice), `holder` (a
`takeTransmit` only, section 18.9), and `forCommandId`, `forWriteId` or
`forSettingsKey` naming the held change. `affected`
has one entry per disturbed device, `{deviceId, deviceName,
deviceShortName, state, holdsTransmit, slices}`, each slice `{sliceId,
letter, frequencyHz, band, mode, adc, streamIndex, effect}`: `state` is
`listening` or `away`, `mode` the slice's `dspMode` value, `band` its
`Band` value, `adc` its receiver's ADC from 0, `streamIndex` its receiver
from 0 (shown as "Receiver `streamIndex` + 1", "ADC `adc` + 1"), `effect`
`moves` or `closes` for a pan move, and for a shared setting also
`changes` (keeps receiving, differently) or `pausesWhileTransmitting`
(section 7.6). A take's `affected` is empty: its choices say
what each would close. A `takeReceiver` choice is `{choice, streamIndex,
adc, centreHz, rateHz, anchorName, slices, devices, takeable, why}`, its
slices `{sliceId, letter, deviceId, deviceName, frequencyHz, mode, band,
txSlice}` and its devices `{deviceId, name, shortName, state,
lastActivitySeconds}`; a free receiver (offered only by Take it back) has
no slices. A `takeSlice` choice is `{choice, sliceId, letter, deviceId,
deviceName, deviceShortName, state, frequencyHz, mode, band, txSlice,
streamIndex, adc, takeable, why}`.

**Answers** (section 9.1): `confirm.proceed {id, choice}` (`choice` -1
for a `panMove` or a `sharedSetting`) or `confirm.cancel {id}`. Nothing a
device sent before its answer changes anything. A device has one open
question; a new one replaces it, and its session ending drops it. A
question expires 60 s after it is sent (`expiresInMs`): a later
`confirm.proceed` is refused "That question has expired. Make the change
again." and changes nothing (iPhone app plan Task 75). On proceed the Core computes
what the change reaches again: when that names a device or an effect the
operator was not shown, the proceed is answered "Waiting for you to
confirm." with `phase` `needsConfirmation`, a new `confirm.request`
follows, and nothing is applied. Otherwise the change is applied exactly
as the original request would have been, and the proceed's
`command.result` carries the readback (ruling 7.4a): for a property
write, `objectKey` and the settled value of every property the write
named in `values` (its side effects on the written object reach the
writer as the usual `delta`); for a settings write, `settingsKey` and
`value` (the stored value) in `values`; for a command, the original
command's own `affected` and `values` (a `requestSliceSampleRate`, which
the Core runs on a later turn, answers the proceed when it has run). The change reaches every other session as a
`delta`. A proceed that is refused ("That question is no longer open.
Make the change again.", "That choice is not in the list. Make the change
again.", "What this change reaches has changed. Make the change again.")
carries no readback. The question is always sent after the answer to the
request that raised it. A `sharedSetting` proceed is also refused "That
setting changed since you asked. Make the change again." when what the
change acts on moved since the question was asked, whoever moved it.
Every slice a question names (the written slice, a `sliceId` argument, the
slices a move carries) must still be the asking device's at proceed. A
question whose slice closes or passes to another owner is dropped at once,
since the Core hands the lowest free id to the next slice; its later
`confirm.proceed` is refused as changed ("That setting changed since you
asked. Make the change again." for a `sharedSetting`, "What this change
reaches has changed. Make the change again." for any other kind) and
changes nothing, and its `confirm.cancel` is accepted.

**`notice`** (Core to device, `sessionHolderVersion` 1 only): `id`,
`kind`, `reason`, `secondsAgo` (whole seconds since it happened, measured
when sent), `takeBack` (boolean), and optionally `byDeviceId`, `byName`,
`byShortName`, `byKind`, `bySource` (`device`, or `radioPtt` when the
radio's own PTT took transmit, its names then "Radio" and its kind
`station`) naming who did it,
`slices` `[{sliceId, letter, frequencyHz, mode, band}]` (closed ones
included) and `change`. Kinds here: `sliceMoved` and `sliceClosed` (no
Take it back), `receiverTaken` and `sliceTaken` (Take it back),
`settingChanged` (section 7.6: `change`, who, no Take it back),
`transmitTaken` (section 18.9: who took transmit, Take it back),
`graceEnded`, `slicesNotRestored` and `antennaKept` (about the device's
own state: no `by` keys, no Take it back). `graceEnded`, "You were away for more than 3
minutes. Your slices are back.", goes right after `snapshot.complete` to a
device let in after its 3 minutes ran out, its `slices` listing any saved
slice that could not be restored (then its words are "You were away for
more than 3 minutes. <n> of your slices could not be restored: all the
radio's receivers are in use."); otherwise `slicesNotRestored`, "<n> of
your slices could not be restored: all the radio's receivers are in
use.", reports those. An away device's notices wait and follow its
`snapshot.complete`; after its 3 minutes they still arrive, after
`graceEnded`, with `takeBack` false. The Core keeps them until the device
returns, is removed, or the Core restarts. A session without
`sessionHolderVersion` 1 is sent neither kind.

While a device is on the air, the rules above that would move or close
its transmit slice are refused instead (the several-devices design, ruling
7.4): that refusal arrives with the transmit holder (a later task).

### 7.6 Settings that affect every device

iPhone app plan Task 75 (the several-devices design, sections 7.1 to 7.4,
rulings 5.11a, 6.1 and 7.1 to 7.8). Some settings belong to the radio,
not to one slice, so a change to one reaches every slice that listens
through what it touches, whoever owns it. A change on this list that
would disturb another device's slices (or, once transmit has a holder,
the holder, when it touches the transmitter) is held and asked, as
section 7.5 describes, with `kind` `sharedSetting`; one that disturbs
nobody, or sets the value already there, applies at once as before. The
requester's own slices never count, nor do slices nobody owns.

| Change | Arrives as | What it reaches |
| --- | --- | --- |
| Sample rate | `requestSliceSampleRate` | Protocol 1: every receiver (the radio's data flow stops); Protocol 2: that receiver. Each other device's slice the narrower window leaves out moves to another receiver or, with none free, closes (the Core's own plan); it closes only once the change is certain, so a change refused after Confirm closes nothing and tells nobody |
| Attenuator, preamp, automatic attenuator | `stepAtt` writes (`attenuationDb`, `enabled`, `preampMode`, `autoAtt...`) | ADC0's receivers |
| ADC1 preamp | `stepAtt` `rx1Preamp` | ADC1's receivers |
| Receive antenna | a slice's `rxAntenna`; `alexAntennas` `rxAntennas`, `rxOnlyAntennas`, `useTxAntennaForRx`; `setAlexRxAntenna` | every receiver on a 1-ADC board; on a 2-ADC board ADC0's (ANT1 to ANT3) and, for a receive-only input, ADC1's; a slice's own write also its receiver's other slices |
| Receive filter policy | `setAlexBpfMode` | the receivers on that filter chain |
| PureSignal | `pureSignalSettings` writes, `transmit` `pureSig`, `ps3.off`, `ps3.single`, `ps3.automatic`, `ps3.applyCurrent`, `ps3.restoreCorrection` | on a 1-ADC board every receiver, `pausesWhileTransmitting`; the transmitter |
| Diversity | a slice's `diversityEnabled`, `diversityPhaseDeg`, `diversityGainDb`, `diversityFineNullEnabled` | receiver 0 on a 2-ADC board, every receiver on a 1-ADC board |
| A shared receiver's noise blanker | a slice's `nbMode`, `nb1Threshold`, `nb1TransitionMs`, `nb1LeadMs`, `nb1LagMs`, `nb2Mode` | that receiver's slices |
| Notches | `notch.add`, `notch.move`, `notch.setActive`, `notch.delete`; `notches` `globalEnabled`, `autoIncrease` | every slice whose passband overlaps the notch (the notches, for the two switches) |
| Receive options | `settings.write` of the receive `DspOptions...Rx` keys (buffer size, filter size, filter type, per mode group) | every receiver |
| Transmit antenna | a slice's `txAntenna` | the transmitter |
| The amplifier, interlock, power limit | `amplifier` `operate`; `configurePgxl`, `disconnectPgxl`, `setPgxlConnectionSettings`, `setPgxlName`, `setPgxlHardware`, `setPgxlNetwork`, `savePgxlSettings`, `setTxInterlockPolicy`, `setPgxlPowerCap`, `configureRfKit`, `disconnectRfKit`, `setRfKitEnabled`; `settings.write` of `PGXL_...` | the transmitter |
| 4O3A on or off | `setFourO3AEnabled` | as the tuner: ADC0's receivers on a 2-ADC board, every receiver on a 1-ADC board; the transmitter |
| The tuner, the RF-Kit amplifier's antenna | `setTgxlAntenna`, `setTgxlOperate`, `setTgxlBypass`, `configureTgxl`, `disconnectTgxl`, `setTgxlName`, `setTgxlNetwork`, `saveTgxlSettings`; `settings.write` of `TGXL_...` and `RfKit_...` | ADC0's receivers on a 2-ADC board, every receiver on a 1-ADC board; the transmitter |

A verb naming another device's slice is refused first, as section 7.3
says. The words of `change` are the Core's: "Attenuator, ADC 1", "0 dB",
"20 dB"; "Sample rate, Receiver 1", "192 kHz", "96 kHz"; "Noise blanker,
Receiver 1", "Off", "NB"; "Diversity phase", "0 degrees", "45 degrees";
"Diversity gain", "0 dB", "6 dB"; "Diversity fine null", "Off", "On"; "4O3A
amplifier and tuner", "Off", "On". A `settings.write` held this way is answered by
`settings.reject` with "Waiting for you to confirm." and the Core's value
(section 8.1), and its `confirm.request` carries `forSettingsKey`.

On proceed the change applies as the original request would have, the
answer carrying the readback (section 7.5), and each disturbed device is
sent a `notice` of kind `settingChanged`: who, `change`, `secondsAgo`,
the slices of its it reached (`slices`), `takeBack` false, and a
`reason` such as "iPhone changed Attenuator, ADC 1 from 0 dB to 20 dB."
followed, where a slice moved, closed or pauses, by "Your slice B moved
to another receiver.", "Your slice B closed: no receiver was free." or
"Your slice B pauses while the radio transmits.". A new write from the
requester to the same thing cancels its open question. A window without
`sessionHolderVersion` 1 is never asked: its change is refused "This
change would affect <names>. Update NereusSDR to confirm changes that
affect other devices.".

**The receive antenna stays put.** Band tracking re-applies a band's
receive antenna when a slice crosses into it. While another device has a
slice on a receiver fed by the ADC that antenna relay feeds (every
receiver on a 1-ADC board, ADC0's on a 2-ADC board), the antenna stays
where it is instead: the tuning goes ahead with no question, and the
device tuning is sent a `notice` of kind `antennaKept`, "The antenna stays
on ANT1 while <other device's short name> listens on it.", with the
slice in `slices` and no `by` keys. It stays when a transmission ends
too; the band's transmit antenna still applies at key-down. Once the
other device's slices have left that ADC, the next band crossing switches
as always; nothing switches on its own when they leave.

**Transmit.** A change that touches the transmitter disturbs the
transmit holder (listed with `holdsTransmit` true), and while the holder
is on the air a change to the transmit path, a Protocol 1 sample rate, or
a change that would move or close the holder's transmit slice is refused
with "<holder's short name> is on the air. Try again when they stop."
(ruling 7.4). Both arrive with the transmit holder (a later task); until
then nobody holds transmit, so a change that touches only the
transmitter disturbs nobody and applies at once, and `state` is never
`transmitting`.

### 7.4 The catalogue

The `catalog` object's `json` is one JSON object (RFC 8259, UTF-8,
compact) holding the values the Core owns and an app shows: the modes, the
Core's filter presets, the tune steps, the AGC, receive and gauge ranges, the
radio's capabilities, the band buttons, the band plans, the waterfall
palettes, the slice colours and the Core's tools (`StationCatalog`, spec section 4.10). An app
draws its controls from it and carries no table of its own, so a Hermes
Lite 2 and an ANAN-G2 each get their own. It is the same for every device
connected to the Core; nothing in it is per device.

**When it changes.** The Core keeps it current: it builds it at start, reads
it again as a session's snapshot is first sent, and rebuilds it, at most
once per turn of its event loop, when a filter preset or the CW pitch
changes in its settings (from its own computer or a window, section 8),
when its band plan data is read again, and when its radio changes (a radio
found after the session began included). A rebuild
that changes nothing leaves `revision` alone; one that changes anything
moves it by one, so the three settings of one preset move it once. The new
value reaches a connected client as a `delta` (section 7.2).

**Size.** At most 256 KiB of `json` for the largest radio
(`StationCatalog::kMaxJsonBytes`); today's are about 40 KiB, most of it the
band plans.

**Units and forms.** A key names its unit (`Hz`, `Db`, `Dbm`, `W`);
numbers are JSON numbers and a whole value is written without a fraction.
Colours are `#RRGGBB`, upper case. Labels are the desktop's own words,
shown as sent. A key an app does not know is ignored; an app given an
empty `json` (the stand-in of section 16.3) has no catalogue yet.

The object has exactly these fourteen keys:

| Key | Holds |
| --- | --- |
| `modes` | `[{id, label, sideband}]`: the 14 modes, `id` the slice's `dspMode` value 0 to 13, `label` its name (`LSB`, `USB`, ..., `RADE-U`, `RADE-L`), `sideband` `lower`, `upper` or `both` |
| `filterPresets` | `{<mode label>: [{slot, label, lowHz, highHz}]}`: each mode's presets from the Core's store, slot 0 first (`F1`), edges signed as a slice's `filterLow` and `filterHigh`; a mode has 1 to 10 |
| `tuneSteps` | `[{hz, label}]`: the step list, smallest first, as the slice's `stepHz` takes it; `label` like `500 Hz`, `1 kHz`, `2.5 kHz`, `1 MHz` |
| `agc` | `{modes: [{id, label}], thresholdDb: {min, max, step}}`: the AGC modes an operator picks (`id` the slice's `agcMode`: `Off`, `Long`, `Slow`, `Med`, `Fast`), and AGC-T's range for `agcThreshold`. The Modes tab's AGC section shows no other range; a later one arrives as another `{min, max, step}` key named after the setting it bounds |
| `receive` | `{afGain, ssqlThresh, amsqThresh, fmsqThresh}`, each `{min, max, step}` for the slice setting of that name, as the desktop's own control holds it: `afGain` 0 to 100 in the AF slider's units, `ssqlThresh` 0 to 100 in the SQL slider's units, `amsqThresh` and `fmsqThresh` -160 to 0 dB; every step 1 |
| `meters` | The gauges an app draws (below) |
| `board` | The radio (below) |
| `bands` | `[{id, label}]`: the desktop's per-pan BAND grid, in its order (160, 80, 60, 40, 30, 20, 17, 15, 12, 10, 6, WWV); `id` is the band as `slice.selectBand` takes it (0 for 160 m to 10 for 6 m, 12 for WWV) and `label` is the button's text. The desktop draws its grid from the same table, so the two cannot differ |
| `bandPlans` | `[{id, name, default, segments: [{lowHz, highHz, label, licence, lowestClass, colour}]}]`: every bundled plan, `id` its file's name (`arrl-us`), `default` true on ARRL (US) alone; `licence` lists the licence classes (`E,G`), empty for a beacon or no transmit; `lowestClass` is the lowest class the segment allows, as the desktop's band-plan strip names it after the label (`PHONE General`): `Tech` when `licence` holds T, else `General` when it holds G, `Extra` when it is exactly `E`, and empty otherwise |
| `palettes` | `[{id, name, stops: [{at, colour}]}]`: the waterfall palettes, `id` the desktop's palette number, `at` from 0 to 1 to three places, lowest first. The Custom palette is each computer's own and is not listed |
| `sliceColours` | `[colour]`: slice A's colour first, one for each slice the radio allows |
| `tools` | `[{id, label, where, offered}]`: the desktop's Tools menu in its order, `where` `station` (works at the Core) or `both`; MIDI Mapping and Macro Buttons are not listed |
| `radioItems` | `[{id, label, offered}]`: Manage Radios, Antenna Setup, Transverters and Protocol Info, in the desktop's Radio-menu order |
| `audio` | `{}` (filled in a later revision) |

`meters`:

| Key | Holds |
| --- | --- |
| `sMeter` | `{minDbm, s9Dbm, maxDbm, dbPerSUnit, redFromDbm, sUnits: [{label, dbm}], overS9: [{label, dbm}]}`: S0 at -127 dBm, 6 dB an S-unit, S9 at -73 dBm, then `+10` to `+60` every 10 dB up to -13 dBm, red from S9 |
| `micLevel` | `{minDb, maxDb, yellowFromDb, redFromDb}`: -40 to +10 dB, yellow from -10, red from 0 |
| `rfPower` | `{minW, maxW, ratedW, redFromW}`: red from the PA rating, full scale 20% past it |
| `swr` | `{min, max, redFrom}`: 1.0 to 3.0, red from 2.5 |

`board`:

| Key | Holds |
| --- | --- |
| `model` | The radio model, as `hpsdrModel` in capabilities |
| `productLabel` | Its name (`ANAN-G2`, `Hermes Lite 2`) |
| `maxSlices` | The slices it allows |
| `attenuator` | `{min, max, step}` in dB for the step attenuator, or `null` without one |
| `preampItems` | `[{id, label}]`: the preamp choices, `id` the `stepAtt` object's `preampMode` |
| `rxAntennas`, `txAntennas` | The main antenna ports (`ANT1` upwards) |
| `rxOnlyInputs` | The receive-only inputs by the product's own labels (`BYPS`, `EXT1`, `XVTR` on an ANAN-G2), empty without them |
| `sampleRates` | The receive rates in Hz the radio offers on the protocol it runs |
| `pureSignal` | Whether it has PureSignal |
| `paRatingW` | Its PA rating in watts |
| `micJack` | Whether it has a microphone input of its own |

`offered` is the Core's: an item is listed as offered once the desktop has
built it (CWX, Memory Manager, CAT Control and Transverters are not yet),
and an app shows only offered items, in their place. The two catalogue
fixtures (section 16.3) hold an ANAN-G2's and a Hermes Lite 2's catalogue
in full.

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
| 2. prefix | `filters/` | station |
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
| 3. whole key | `MoxTimeOutEnabled` | station |
| 3. whole key | `MoxTimeOutSeconds` | station |
| 3. whole key | `PingTimeOutEnabled` | station |
| 3. whole key | `PingTimeOutSeconds` | station |
| 3. whole key | `PingTimeOutHost` | station |
| 3. whole key | `RemoteMoxTimeOutEnabled` | station |
| 3. whole key | `RemoteMoxTimeOutSeconds` | station |
| 3. whole key | `RxOnly` | station |
| 3. whole key | `DisableHfPa` | operatorLocal |
| 3. whole key | `ExtendedTxAllowed` | operatorLocal |
| 3. whole key | `PreventTxOnDifferentBandToRx` | operatorLocal |

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
  recognise its own echo. It sends that `settings.value` to every device's
  session, each of which holds every station-scoped key; only the writer
  finds its own `origin` in it (iPhone app plan Task 72). A change the Core makes itself goes out as
  `settings.value` with an empty origin. A removal goes out as
  `settings.value` with no property entry.
- `settings.remove` removes a station-scoped key; the station ignores (and
  logs) a remove of an operator-local key.
- A refused write or remove gets `settings.reject`: the key, the station's
  own value as the property entry when it has one, and a `reason`. The
  client puts that value back. It goes to the session that wrote, and to
  no other.

A `settings.write` that would disturb another device (the several-devices
design's section 7.1 list, section 7.6 here) is held as section 7.3 says:
`settings.reject` with the reason "Waiting for you to confirm." and the
Core's value, then a `confirm.request` with `forSettingsKey`; its proceed
carries `settingsKey` and `value` as its readback (section 7.5). A
`settings.remove` returns its key to the default, live, so it is checked
exactly as a write of the default is: held and asked the same way, with
`change.to` "Default", and its proceed carries `settingsKey` alone as its
readback, since the key is gone.

A slice's own keys, `Slice<N>/...`, are written and removed only by the
device that owns slice N (the several-devices design, ruling 5.9). From
any other device, and for an id no live slice holds, they get
`settings.reject` with the Core's value and the reason "That slice belongs
to <the owner's name>. It can be changed only there." ("the Core" when
nobody owns it).

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
`meterReadingsVersion` 1 a taken `MultimeterDelayMs` (Setup > Display >
Multimeter > Polling delay), or its removal, sets the Core's meter pump
rate at once, clamped to 10 to 2000 ms (a removal returns the 100 ms
default); it only changes how often the Core reads its meters, so it is
taken on and off the air, as a local window changes it. At
`transmitSettingsVersion` 6 the same off-air rule holds for Setup > PA's
keys, `hardware/<mac>/pa/...` (the PA profiles: the profile list
`pa/profile/_names`, each profile `pa/profile/<name>`, and the active
profile `pa/profile/active`) and `hardware/<mac>/paCalibration/...` (the
PA forward-power table, `boardClass` and `calPoint1` to `calPoint10`),
from a peer at agreed minor 11. A taken key, or its removal, applies to
the Core's PA profiles or calibration at once, and a window reloads its
copies of both when those keys change. At `transmitSettingsVersion` 8 the
same off-air rule holds for Hardware Config's OC transmit pins,
`hardware/<mac>/oc/tx/<band>/pin<n>` (the HF and SWL TX matrices, and the
resets, which write them), from a peer at agreed minor 11; its OC pin
actions, `hardware/<mac>/oc/actions/pin<n>/action`, and its TX Display Cal
and Volts/Amps Calibration, `hardware/<mac>/cal/txDisplayOffset`,
`cal/paSens` and `cal/paOffset` with the Calibration tab's copies
`hardware/<mac>/paCalibration/cal/{txDisplayOffset,paSens,paOffset,paDefaultRestored,logVoltsAmps}`,
are taken on the air too, because Thetis changes them while transmitting
(its TX pin boxes alone are greyed while MOX is on, unless OC hot switching
is allowed, which NereusSDR does not build). On a station that allows remote
transmit these keys are a permitted session's (section 18), and the rule is
the change's, not the holder's: an OC transmit pin still waits while the
radio is on the air, whoever holds transmit, and the pin actions and the
calibration are still taken on the air. A taken OC key applies to the
Core's OC matrix, which the radio's codec reads for every frame, once the
radio is back on receive; a taken calibration key applies to the Core's
calibration at once. The N2ADR switch (`hardware/<mac>/hl2IoBoard/n2adrFilter`)
on a Core's HL2 applies its whole preset, transmit pins included, likewise
once the radio is on receive. User Dig Out is the `transmit` object's
`userDigOut` (version 1). The rest of the transmit-side hardware keys stay
refused: the HL2's TX buffer latency and PTT hang
(`hl2/{pttHangMs,txLatencyMs}`) and the Alex TX low-pass band edges
(`alex/lpf/...`), which both windows hide until they are applied; and
the OC hot switching and external PA keys. The Alex high-pass switches
for transmit (`alex/master/{hpfBypassOnTx,hpfBypassOnPs,disable6mLnaOnTx}`)
are taken from a peer at agreed minor 11 offered `radioHardwareVersion` 7
(parity Task 14) and applied to the Core's radio at once, because Thetis's
setters (console.cs `DisableHPFonTX`, `DisableHPFonPS`, `Disable6mLNAonTX`)
apply them with no MOX check, as it does the TX antennas; neither window
greys them on the air. They follow the TX antennas' rule (section 18.4):
the device holding transmit changes them on the air, another device's change
waits ("<holder's short name> is on the air. Try again when they stop."), and
off the air another device's change is asked of the holder (the
several-devices design, table 7.1, "Transmit antenna"). From an older
peer they stay refused with the transmit reason, and a window whose Core
does not offer 7 shows them disabled with "This Core cannot change these
high-pass switches for this app. Updating the Core may help."

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
| `StationLabel` | `station.rename` | "This Core keeps its own name. Update this app to rename it." |
| `StationKeyBackupAcknowledged` | `station.acknowledgeKeyBackup` | generic (below) |

Matching is case-insensitive. The reasons in the table are plain operator
wording. `StationLabel` is empty until the first rename, and while it is
empty the Core's label follows `StationCallsign` (`StationLabel::current`).
`StationKeyBackupAcknowledged` holds the fingerprint of the identity key
that was acknowledged, so a replaced key asks again. A remove of either is
refused with the table's reason too. The generic families give "The Core changes these settings only
through their own controls." for a write, and "Change these settings with
their own controls on this Core." for a remove.

## 9. Commands

### 9.1 Invoke and result

A client asks the station to act with `command.invoke`: a `verb`, an `id`
and `args`, a list of property entries. The station answers each with
`command.result`: the same `verb` and `id`, `accepted`, `reason` (empty on
success), `affected` (the object keys the command changed) and, for
commands that return data, `values`, a list of property entries. Every
`command.result`, including a later one (a PureSignal action's later
phases, a sample-rate change answered on a later turn), goes to the session
that sent the `command.invoke`, and to no other. Each client counts its
own ids, so two devices may use the same `id` at once: the station tells
their commands apart by the session that sent each, never by `verb` and
`id` alone. A file a device is
sending with `dspAssets.beginImport` belongs to that device's session: it is
cancelled when that session ends, and another device leaving never touches
it. For the
`nnr.*`, `ps3.*` and `dspAssets.*` families the `id` must be a whole number
from 1 to 4294967295 or the message is refused.

A PureSignal action can answer more than once: each result carries a
`phase` value of `accepted`, `pending`, `completed` or `failed`, and the
last is `completed` or `failed`. The phase travels in the result's
`values`, as a `utf8` property entry named `phase` (ordinal 0), beside any
other values the action returns: `session-verbs-ps3` shows it
(`{"kind": "utf8", "name": "phase", "ordinal": 0, "value": "accepted"}`,
then `"completed"` for `ps3.off`, and `"failed"` for `ps3.saveCorrection`).
A PureSignal request refused before the action starts (one with arguments
it does not take) answers once, with no `values` and so no phase.

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
| `slice.selectBand` | `sliceId` i64, `band` i64 | `bandSelectVersion` | 1 | 11 |
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
| `tx.setTxSlice` | `sliceId` i64 | `remoteTxVersion` | 1 | 11 |
| `tx.key` | `trigger` utf8 | `remoteTxVersion` | 1 | 11 |
| `tx.unkey` | `epoch` i64 | `remoteTxVersion` | 1 | 11 |
| `tx.tune` | `on` bool | `remoteTxVersion` | 1 | 11 |
| `tx.twoTone` | `on` bool | `remoteTxVersion` | 1 | 11 |
| `tx.keepalive` | `sequence` i64, `epoch` i64 | `remoteTxVersion` | 1 | 11 |
| `tx.take` | `holderEpoch` i64 (optional), `shownKeyed` bool (optional) | `remoteTxVersion` | 2 | 11 |
| `tx.tunerTune` | `on` bool | `remoteTxVersion` | 2 | 11 |
| `setAlexTxAntenna` | `band` i64, `antenna` i64 | `radioHardwareVersion` | 6 | 11 |
| `requestIoBoardI2c` | `bus` i64, `address` i64, `register` i64, `write` bool, `value` i64 | `radioHardwareVersion` | 7 | 11 |
| `setIoBoardOutput` | `pin` i64, `on` bool | `radioHardwareVersion` | 7 | 11 |
| `dsp.filterResponse` | `sliceId` i64, `highResolution` bool | `dspInfoVersion` | 1 | 11 |
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
| `notch.addAtSlice` | `sliceId` i64 | `notchControlVersion` | 2 | 5 |
| `devices.revoke` | `id` utf8 | `deviceAdminVersion` | 1 | 11 |
| `station.rename` | `label` utf8 | `deviceAdminVersion` | 1 | 11 |
| `station.acknowledgeKeyBackup` | none | `deviceAdminVersion` | 1 | 11 |
| `station.retireToken` | none | `deviceAdminVersion` | 1 | 11 |
| `pairing.open` | none | `pairingVersion` | 1 | 11 |
| `pairing.close` | none | `pairingVersion` | 1 | 11 |
| `session.leave` | none | `sessionHolderVersion` | 1 | 11 |
| `confirm.proceed` | `id` i64, `choice` i64 | `sessionHolderVersion` | 1 | 11 |
| `confirm.cancel` | `id` i64 | `sessionHolderVersion` | 1 | 11 |
| `notice.takeBack` | `id` i64 | `sessionHolderVersion` | 1 | 11 |

<!-- /surface -->

The table's capability columns are the gate the desktop client applies
before sending (section 6.2).

These command groups need a sentence beyond the table:

- **Slices** (iPhone app plan Task 73). With several devices on one Core,
  `addSlice` and `addSliceOnPan` make a slice the asking device owns;
  `setActiveSliceById` makes one of the asking device's own slices its
  active slice, and a slice's `active` means "its owner's active slice", so
  each device sees one active slice among its own and another device's
  choice never moves it. The station's own duties that exist once per
  radio (the FreeDV Reporter's frequency, TCI's per-slice broadcasts)
  follow the most recent choice by any device. `removeSlice`,
  `setActiveSliceById`, `nnr.*`, `notch.add`, `notch.addAtSlice`,
  `slice.selectBand`, `requestSliceSampleRate`, `requestStreamCentre` and
  `requestStreamCtunPinned` naming a slice that is not the requester's are
  refused (section 7.3).

- **A slice's band buttons.** `slice.selectBand` (`sliceId`, `band`,
  both `i64`; `bandSelectVersion` 1, agreed minor 11) does what a band
  button of the desktop's per-pan BAND grid does for that slice: the Core
  runs its own band change (`RadioModel::onBandButtonClicked`), so the
  slice gets back the frequency, mode, filter and the rest it last had on
  that band, or the band's starting frequency and mode on a first visit,
  and saves what it leaves for the band it left. `band` is an `id` from
  the catalogue's `bands` (section 7.4). A band the slice is already on
  is accepted and changes nothing, as on the desktop. `accepted` names
  the slice (`slice:<id>`) in `affected`; the change reaches it in the
  next `delta`. The refusals: "That receiver is no longer on the Core."
  (an unknown `sliceId`), "The Core has no band button for that band."
  (a `band` the catalogue does not list), the desktop's own reason for a
  locked slice ("Band 40m ignored: the slice is locked. Unlock it to
  change bands."), and "The request to change band was not understood."
  (arguments it does not take). The desktop offers every grid band on
  every radio and changes band while the radio is on the air, so the
  Core refuses neither. A peer below agreed minor 11 gets "Update this
  app to change bands on this Core.", and a Core that sends
  `bandSelectVersion` 0 answers "This Core cannot change bands for an
  app." A remote desktop window sends it too, for the slice its band
  button belongs to (a pan's BAND flyout acts on that pan's slice, the RX
  applet and a container on theirs), so its band changes use the Core's
  band memory as a band button at the Core does; to a Core at
  `bandSelectVersion` 0 it writes the slice's frequency and mode as
  before.
- **A notch at a slice.** `notch.addAtSlice` (`sliceId` `i64`;
  `notchControlVersion` 2, agreed minor 5) does what the desktop's +TNF
  button does for that slice: the Core puts a notch of 200 Hz
  (`NotchModel::kDefaultNotchWidthHz`) at the slice's demodulated
  frequency (the VFO, RIT and the DIGU/DIGL click-tune offset) moved by
  the middle of its receive filter (Thetis TNFAdd with
  notchSidebandShift), through `RadioModel::addTnfForSlice`, the one
  function the desktop's button calls too. The add is `notch.add`'s, so
  its rules and refusals are too (the notch control document): an
  unknown `sliceId` is refused with "That receiver is not on this Core",
  a second press on the same signal with "A notch already exists within
  10 Hz", and arguments it does not take with "This notch change is not
  one this Core understands.". An accepted result is `notch.add`'s:
  `affected` `["notches"]` and the values `revision` and `id`. A peer
  below agreed minor 5 gets "Update this app to change notches on this
  Core.".
- **The filter policy.** `setAlexBpfMode` sets one receive filter chain's
  filter policy (`chain` 0 or 1; `mode` 0 Auto, 1 Force filter, 2 Force
  bypass), the call the Core's own filter policy dialog makes on Apply.
  The Core saves it for its radio and publishes each chain's state on the
  `radio` object (`rxFilter0Mode` to `rxFilter1Reason`) in a `delta`. The
  policy picks the receive band-pass filter only, so a receive-only Core
  applies it too.
- **The filter graph's curve** (parity Task 16, `dspInfoVersion` 1).
  `dsp.filterResponse` asks the Core for the high-resolution filter graph's
  curve for the receiver of `sliceId`, computed from that receiver's
  channel as the Core's own filter graph computes it (the channel's filter
  edges and rate, WDSP's `fir_bandpass` taps, a 4096-point FFT). An
  accepted result carries the `values` `startHz` and `stepHz` (f64) and
  `magnitudesDbJson` (utf8, a JSON array): the magnitude at `startHz` +
  k * `stepHz` for each k from 0, in dB with 0 at the peak and -120 at
  the floor, rounded to 0.001 dB (today 2049 values from 0 Hz to half the
  channel's rate). A window resamples them to its graph's width as it
  would its own channel's curve. With `highResolution` false no curve is
  wanted: the result is accepted with `stepHz` 0 and an empty array. A
  window asks while Setup > DSP > Options > "High-resolution filter
  characteristics in filter graph" is on, one request at a time, and asks
  again when the slice's filter, mode or minimum notch width (which moves
  with the rate) changes. It is a read: it reaches no radio, so it is
  answered on and off the air, for any slice. Refusals: "The Core has no
  such slice.", "The Core's receiver for this slice is not running.",
  "The Core cannot work out this filter's curve.", and "The Core could not
  read this request." for arguments it does not take. A window shows no
  notice for a refusal; its graph keeps the plain passband.
- **The HL2 I/O board's I2C tool and output pins** (parity Task 14,
  `radioHardwareVersion` 7). `requestIoBoardI2c` is HL2 Options' I2C
  Control tool: one read (`write` false) or write (`write` true) on the
  Core's radio's I2C bus (`bus` 1, the HL2's daughterboard bus; `address`
  0 to 0x7F; `register` and `value` 0 to 255; `value` is ignored on a
  read). The Core runs it as its own tool does
  (`RadioModel::requestIoBoardI2c`). A read is answered when the radio
  answers, with `values` `value` (i64: the four bytes the radio returned,
  C1 << 24 | C2 << 16 | C3 << 8 | C4, so the register's own byte is
  `value` & 0xFF), or refused "The radio did not answer the I2C request."
  once 21 ms pass after the read went out unanswered (mi0bot's 20
  one-millisecond polls after the first) or the radio goes away; the
  answer comes on a later turn, named for the session that asked. A write
  is answered once it is queued, and a write to the output register (169
  at 0x1d) is read back into `ioBoard` `outputs`. `setIoBoardOutput` is
  Pin Control: output `pin` (0 to 7) `on` or off, written to the output
  register and read back into `outputs`, as mi0bot's strip click does; it
  needs the board found ("The radio's I/O board was not found."). A write
  and an output pin are refused while the radio is on the air, with "The
  radio is on the air. Try again when it stops.", because they reach the
  I/O board and the N2ADR filter board in the transmit path; a read is
  not. Other refusals: "The radio is not connected, so its I2C bus cannot
  be reached.", "Only a Hermes Lite 2 has this I2C bus.", "Only I2C bus 1
  can be reached.", "Choose an I2C address from 0x00 to 0x7F.", "Choose a
  register and a value from 0x00 to 0xFF.", "The I/O board's outputs are
  numbered 0 to 7.". A local window's tool follows the same rules.
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
- **The Core's devices.** Taken only from a peer at agreed minor 11 whose
  hello declares `deviceAuth`, the peers the `devices` object goes to;
  from any other the station refuses them with "Update this app to manage
  this Core's paired devices." (`StationServer::onTransportText`).
  `devices.revoke` removes the paired device whose `id` (as in `listJson`)
  it names; the Core ends that device's connection (section 12.4), and a
  device may revoke itself, its connection ending just after the result.
  The last paired device is refused while no token is active, "Pair
  another device first, or reset this Core from its own computer.": its
  removal would leave the Core unclaimed, and only the console's reset
  does that (section 3.6). A device enrolled through the token
  (section 3.5) is refused while the token is active, "Stop accepting the
  pairing token first, then remove this computer.": the token would enrol
  it again at its next sign-in.
  `station.rename` stores a label (section 8.2): a callsign of letters,
  digits and `/`, then optionally `/` and up to 32 letters, digits, `-` or
  `_`; any other is refused with a reason that states the rule.
  `station.acknowledgeKeyBackup` records the operator's backup of the
  identity key. `station.retireToken` retires the token (section 3.3); it
  is refused, "Pair a device with this Core first, so a device can still
  sign in once the pairing token stops working.", until a device is paired,
  and accepted with nothing to do on a Core without a token. Each accepted
  one names `devices` in `affected`; its change reaches the object in the
  next `delta`.
- **The pairing window.** `pairing.open` and `pairing.close`
  (`pairingVersion` 1) go to the same peers as the device commands, and
  from any other the station refuses them with "Update this app to pair
  new devices with this Core.". `pairing.open` reopens the window on a
  claimed Core (nothing to do while it is open) and answers with `values`
  holding `code` (`utf8`), the current code. Only a connection signed in
  with a paired device's key may send it: one signed in with the token is
  refused with "Open pairing from a paired device or from the Core's
  console." (section 3.6). `pairing.close` closes a reopened window, from
  either kind of connection. Either one with arguments is refused ("The
  request to open pairing was not understood.", "The request to close
  pairing was not understood.").
- **Leaving on purpose.** `session.leave` (`sessionHolderVersion` 1, iPhone
  app plan Task 71) ends the device's session with no away time: its
  place is free at once (section 12.4). After the accepted result the
  station closes the connection, with no `session.end`; the client closes
  its end too. With arguments it is refused, "The request to leave the
  Core was not understood.". From a peer without `sessionHolderVersion` 1
  it is refused as a verb the station does not route is (section 9.2),
  and the connection stays up.
- **Answering the Core's questions** (`sessionHolderVersion` 1, iPhone
  app plan Task 74; section 7.5). `confirm.proceed {id, choice}` applies
  the change a `confirm.request` held (`choice` -1 when the kind has none,
  a choice's `choice` for a take); `confirm.cancel {id}` drops it and
  changes nothing; `notice.takeBack {id}` asks a `receiverTaken` or
  `sliceTaken` notice's take the other way, answered "Waiting for you to
  confirm." with `values` `phase` `needsConfirmation` and a
  `confirm.request`. A missing or renamed argument is refused, "The Core
  could not read this request."; an id that names no open question, "That
  question is no longer open. Make the change again.". From a peer without
  `sessionHolderVersion` 1 each is refused as a verb the station does not
  route (section 9.2).
- **Receiver requests from several devices** (section 7.5).
  `requestStreamCtunPinned` from a device that does not anchor the
  receiver is refused; `requestStreamCentre` from the anchor that would
  leave another device's slice outside is held and asked, from another
  device it takes the panadapter to a free receiver. `addSlice`,
  `addSliceOnPan` and those refused because every receiver (or slice) is
  in use name the devices holding them, and a device with the feature is
  then asked to take one.

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
unless the minor is at least 11 and the version at least 4, and its HL2
link unless the minor is at least 11 and the version at least 5. A message
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

At version 5 (remote-window parity Task 14) the radio section also carries
the Core's Hermes Lite 2 link, from the bandwidth monitor its own HL2 I/O
tab, Radio Status and Diagnostics > Connection Quality read, only while
the radio is connected and only on a radio that has the monitor (the HL2):
`hl2RxBytesPerSecond` and `hl2TxBytesPerSecond` (bytes per second received
from the radio on EP6 and sent to it on EP2, finite and not negative),
`hl2Throttled` (bool: the LAN link is throttled) and `hl2SequenceGaps` (EP6
sequence gaps since the radio connected, a whole number). The throttle
event count is not sent: a remote window shows whether the link is
throttled now and says the count is not sent. Those three surfaces show
the Core's figures "from the Core", and unavailable, never 0, when absent
or out of date.

<!-- surface:telemetry -->
<!-- Generated by scripts/render-link-tables.py from tests/data/link/v1/surface.json. Do not edit by hand. -->

Message kind `station.metrics.v1`.

| `stationTelemetryVersion` | Minimum minor | Field paths | Field paths added |
| --- | --- | --- | --- |
| 1 | 3 | 15 | `audio.active`, `audio.contextGeneration`, `audio.encodeFailuresPerSecond`, `audio.encodedPacketsPerSecond`, `audio.sendAcceptedPerSecond`, `audio.sendRejectedPerSecond`, `audio.sourceDropsPerSecond`, `audio.sourceFramesPerSecond`, `radio.connected`, `radio.rttAgeMs`, `radio.rttMs`, `radio.rxMbps`, `radio.txMbps`, `sampledElapsedMs`, `sequence` |
| 2 | 10 | 22 | `host.hottestZoneCelsius`, `host.hottestZoneName`, `host.memoryAvailableKiB`, `host.memoryTotalKiB`, `host.processCpuPercent`, `host.processResidentKiB`, `host.systemCpuPercent` |
| 3 | 11 | 26 | `receivers[].inputDelayMs`, `receivers[].loadPercent`, `receivers[].skippedInputMs`, `receivers[].sliceId` |
| 4 | 11 | 35 | `radio.jitterMs`, `radio.paCurrentAmps`, `radio.paTemperatureCelsius`, `radio.paVolts`, `radio.packetGapMs`, `radio.packetLossPercent`, `radio.sampleRateHz`, `radio.supplyVolts`, `radio.udpPacketsSeen` |
| 5 | 11 | 39 | `radio.hl2RxBytesPerSecond`, `radio.hl2SequenceGaps`, `radio.hl2Throttled`, `radio.hl2TxBytesPerSecond` |

<!-- /surface -->

## 11. Media control

`media.control` carries one operation object in `payload`, with an `op`
key naming it. The operations, their exact keys and their sequencing are
specified in
[remote media control version 1](2026-09-20-remote-media-control-v1.md);
the table lists the keys each operation carries as the code builds and
checks them. A whole `media.control` message over 128 KiB is refused. The
station accepts media control from each admitted session, for its own
media, after `snapshot.complete`, when media is available (iPhone app plan
Task 76; the several-devices design, ruling 9.1). Every admitted device has
its own media: its own media connection, display endpoints (at most 8),
receiver streams (at most 4) and headphones mix, and one device's
`media.control` never reaches another's. A device subscribes displays, and
asks for receiver streams, only for its own slices: a `subscribe` naming
another device's slice gets an `allocation-result` (or `rejected`) "That
slice belongs to another device.", and a `receiver-audio` naming one is
answered as a slice that is not there (`slice-removed`). When a slice
passes to another device, the old owner's displays on it retire as a
removed slice's do (reason "slice removed", on the `allocation-result`
for a budget-aware app, else `rejected`), and its receiver stream on it
stops as `slice-removed`: that device's view has destroyed the slice. A
receiver's
spectrum is computed once for everyone watching it: two devices' pans on
one receiver share its engine, which runs at the largest size and highest
rate any of them was granted, and a grant limited by that engine says
`sharedEngine` as it does between one device's pans. Each device hears its
own slices: its main audio (the speakers' mix, or its whole program for an
app without the headphones mix) and its headphones mix sum only its own
slices, each with its own gain, pan, mute and route; the Core's own output
plays only the station device's slices. A device that leaves ends its own
media and no other's. Telemetry (`station.metrics.v1`) goes to every
session that negotiated it, each on its own sequence. **Transmit joins
here:** the transmit holder's mix carries the transmit monitor once remote
transmit exists (Task 36). The `subscribe` fields that
come with `displayExtrasVersion` (`peakBlobs`, `activePeakHold`,
`noiseFloor`, `waterfallLevels`, `normalize`, `calibrationOffsetDb`,
`averageTimeMs`, `waterfallAverageTimeMs`), their ranges and what the Core sends for them are
specified in [display extras v1](2026-09-23-display-extras-v1.md).

<!-- surface:mediaControl -->
<!-- Generated by scripts/render-link-tables.py from tests/data/link/v1/surface.json. Do not edit by hand. -->

Message kind `media.control`; the operation's keys sit in `payload`.

Client to station:

| Operation (`op`) | Capability | Fields always present | Fields present with | Object-valued fields |
| --- | --- | --- | --- | --- |
| `audio` | `remoteMediaVersion` | `connectionId`, `enabled`, `op`, `revision` | `profile` with audioProfileVersion, remoteAudioStatusVersion | none |
| `candidate` | `remoteMediaVersion` | `candidate`, `connectionId`, `mid`, `op` | none | none |
| `clarity-retune` | `displayExtrasVersion` | `connectionId`, `endpointId`, `op` | none | none |
| `clock-probe` | `audioClockVersion` | `connectionId`, `id`, `op`, `t0` | none | none |
| `description` | `remoteMediaVersion` | `connectionId`, `op`, `sdp`, `type` | none | none |
| `headphones-audio` | `headphonesMixVersion` | `connectionId`, `enabled`, `op`, `profile`, `revision` | none | none |
| `keyframe` | `remoteMediaVersion` | `connectionId`, `contextGeneration`, `endpointId`, `op` | none | none |
| `receiver-audio` | `receiverAudioVersion` | `connectionId`, `enabled`, `op`, `profile`, `revision`, `sliceId` | none | none |
| `start` | `remoteMediaVersion` | `connectionId`, `op` | `audioProfileVersion` with audioProfileVersion; `headphonesMixVersion` with headphonesMixVersion; `receiverAudioVersion` with receiverAudioVersion; `remoteTxVersion` with remoteTxVersion | none |
| `subscribe` | `remoteMediaVersion` | `centreHz`, `connectionId`, `endpointId`, `fftSize`, `fps`, `framesPerLine`, `maxDbm`, `minDbm`, `op`, `pixels`, `revision`, `sliceId`, `spanHz`, `tier`, `trace`, `waterfall`, `wideSpanFactor`, `windowType` | `activePeakHold` with displayExtrasVersion; `averageTimeMs` with displayExtrasVersion; `calibrationOffsetDb` with displayExtrasVersion; `decimation` with spectrumGrantVersion; `extendedView` with remoteWidebandDisplayVersion; `noiseFloor` with displayExtrasVersion; `normalize` with displayExtrasVersion; `peakBlobs` with displayExtrasVersion; `waterfallAverageTimeMs` with displayExtrasVersion; `waterfallLevels` with displayExtrasVersion | `activePeakHold`: {enabled, fallDbPerSec, holdMs}; `noiseFloor`: {enabled, shiftDb}; `peakBlobs`: {count, fallDbPerSec, holdMs, insideOnly}; `trace`: {averageAlpha, averageMode, detector}; `waterfall`: {averageAlpha, averageMode, detector}; `waterfallLevels`: {highDbm, lowDbm, mode, offsetDb} |
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
- Openings (connections whose TLS or upgrade has not finished) are
  counted apart from connections: at most 64 at once
  (`StationServer::kMaxUnfinishedOpenings`) and 2 from one address or IPv6 /64
  (`kMaxHandshakesPerAddress`, counted as below), each until it opens, is
  refused or reaches the 10 s opening deadline (section 2). A new
  connection at either limit closes the oldest unfinished opening (from
  its own address first, then the oldest of all) and takes its place, so
  openings that never finish do not keep a device out, and a client that
  redials while its own abandoned dials are still opening gets its newest
  dial through. The total is 64, not the 24 connections below, because it
  is what a flood must fill to push a real device's opening out: with 2
  per address that takes 64 connects from at least 32 addresses or /64s
  within the device's own opening time. Each opening costs one file
  descriptor; `nereusd` runs under the default limit of 1024
  (`packaging/nereusd.service.in` sets no `LimitNOFILE`), so 64 openings
  and the 24 connections stay far below it.
- The station accepts at most 24 connections at once
  (`kMaxConcurrentPeers`), counting every socket, signed in, connecting or
  pairing: four devices each reconnecting with an old socket not yet
  noticed dead and four racing attempts, and a fifth device's four. The
  next one gets `session.end` "The Core already has as many connections
  as it allows. Try again shortly.", `retryable` true, before any `hello`,
  because a reconnecting client meets it while its own dead sockets
  drain.
- At most 4 devices hold places at once (`kMaxDeviceSessions`, iPhone app
  plan Task 71): sessions let in, devices away in their 3 minutes, and a
  hosting desktop's own window. Connections still connecting and pairing
  connections take no place.
- Of those, one address may hold at most 2 that are still connecting
  (their snapshot not yet sent; `kMaxHandshakesPerAddress`), so one host
  cannot hold every slot by redialling within the connect deadline. An
  IPv4 address counts by itself, and an IPv4-mapped IPv6 address counts
  as its IPv4 address. Any other IPv6 address counts by its /64 prefix,
  because one host can dial from every address in its /64; a household on
  one /64 therefore shares the 2, as one behind IPv4 NAT does. Signed-in
  sessions are not counted. The next one from that address or /64 gets
  the same `session.end`, `retryable` true. A connection
  with no address of its own (the relay's) is not counted by address.

### 12.4 Ending, admission and retryable

`session.end` carries a `reason` and `retryable`. `auth.result` carries
`retryable` too. A client redials only after a retryable end; after one
that is not retryable it stops and tells the operator.

Both may carry `code`, a stable token for the end (`SessionEndCode` in
`SessionMessages.h`): every permanent end the station sends has one. A
client reads the code where it is present and falls back to the reason text
for an older station, which sends none; an older client ignores the key.
The reason stays what the operator reads. A `code` on the wire is a
non-empty string; an empty one is refused like any mistyped key.

| Cause | Message | `retryable` | `code` |
| --- | --- | --- | --- |
| Wrong token | `auth.result` accepted false, "The Core did not accept this app's pairing token. Check the token saved for this Core.", then the station closes | false | `wrongToken` |
| Token checks locked out (section 3.3) | `auth.result` accepted false, "The Core is refusing pairing tokens for a while after too many wrong ones. Try again later." | true | none |
| A token on a Core without one (section 3.3) | `auth.result` accepted false, "This Core uses paired devices. Pair this device first." | false | `pairingRequired` |
| A device the Core has not paired (section 3.5) | `auth.result` accepted false, "This device is not paired with this Core. Pair it first." | false | `deviceNotPaired` |
| A device sign-in that does not prove itself (section 3.5) | `auth.result` accepted false, "This device could not prove it is paired with this Core." | false | `deviceProofFailed` |
| Device sign-ins limited (section 3.5) | `auth.result` accepted false, "The Core is refusing sign-ins from this device for a while after too many failed ones. Try again later." | true | none |
| No shared major (section 6.1) | `session.end` naming both sides' versions and the side to update | false | `linkVersion` |
| Message the station cannot decode (section 13) | `session.end` "The Core could not read a message from this app." | false | `protocolError` |
| Out-of-order handshake (section 5.1) | `session.end` | false | `protocolError` |
| The same device connected again (section 5.1): its older connection | `session.end` "This device connected again." | false | `sameDevice` |
| Every place on the Core is taken (section 5.1), a device that declared `sessionHolder` | `session.end` "The Core already has four devices connected." | true | none |
| Every place on the Core is taken (section 5.1), an older window | `session.end` "The Core is full. Update NereusSDR to take a device's place, or try again later." | true | none |
| A window without the several-devices feature, with no slice for it at sign-in (section 7.5) | `session.end` "All the radio's slices are in use. Try again when another device closes one." or "All the radio's receivers are in use. Try again when another device frees one." | true | none |
| Such a window's last slice taken by another device (section 7.5) | `session.end` "<taker's name> took the receiver this app was using. Update NereusSDR to share the Core." | false | `takenOver` |
| Connection limit reached | `session.end` | true | none |
| Connect deadline expired | `session.end` | true | none |
| Heartbeat timeout | `session.end` | true | none |
| Station shutting down | `session.end` "The Core is shutting down." | true | none |
| The connection's device was removed (`devices.revoke`, the Core's console, a reset) | `session.end` "This device was removed from the Core." | false | `deviceRemoved` |
| A connection signed in by token when the token is retired (`station.retireToken`) | `session.end` "This Core uses paired devices. Pair this device first." | false | `pairingRequired` |

A device's connection ends whatever removed the device
(`DeviceStore::deviceRemoved`): the command, the Core's console or a reset.
The connection that asked is ended just after its own `command.result`.

A pairing connection (section 3.6) ends after `pair.accept`, the
station's `pair.confirm` or `pair.fail`, with no `session.end`: the reason
a client acts on is `pair.fail`'s.

One code is defined for an end the station never sends:
`identityChanged`, which a client uses for its own end when the Core's
certificate binding or identity key is not the one it paired with. The
desktop client ends that way before it sends its own `hello`: a saved Core
whose `hello` shows no identity or another key, or whose `certBinding` does
not verify for the certificate the connection presented, is refused and
never trusted silently. A new certificate whose binding verifies is
accepted without a question, whatever pin was saved.

The one end the station sends today with `takenOver` is an older
window's last slice taken (section 7.5), with its own words above; the
fifth device's takeover, which will also carry it, is a later version's. The takeover and version
reasons are worded in one place,
`src/core/session/SessionEndReasons.{h,cpp}`: "Another app at
*address:port* connected to the Core and took over. Connect again to take
it back." and "This Core runs link version *N* and this app runs version
*M*. Update the Core." (or "Update this app." when the app is the older
side). An app that offers its own next steps for these two (take the Core
back, check for updates) tells them apart by the `code` (`takenOver`,
`linkVersion`) and falls back to these exact words for an older station.
The desktop client reads the code (`SessionEndReasons::read`, iPhone app
plan Task 18): `takenOver`, `linkVersion`, `deviceRemoved` (and
`deviceNotPaired`, the same notice: pair this computer again),
`pairingRequired`, and its own `identityChanged` each choose the window's
stop message, whatever the words; the words give only the other app's
address and the two versions where they are present. It reads the words
(`SessionEndReasons::parse`) only for an end that carries no code, from a
Core older than the code.

**Several devices** (iPhone app plan Task 71). Up to four devices hold
places at once, and no sign-in ever ends another device's session. A
device's own newer connection replaces its older one, which ends with
`sameDevice`, not retryable, so the two do not trade places. A paired
device whose session ends without `session.leave` (a lost link, the
heartbeat's end, a closed socket, an app its system stopped) is **away**
for 3 minutes (`graceMs` 180000), keeping its place; signing in again
within them is the same-device case, let in with no question. When the 3
minutes end its place is freed and the Core keeps that its time ran out
until the device next signs in, is removed, or the Core restarts. A window
signed in with the token and no key, and a device that leaves with
`session.leave` (section 9.1), frees its place at once; so does removing a
device, away or not. The heartbeat timeout stays retryable: that end is
what starts a device's 3 minutes.

**What happens to a device's slices** (iPhone app plan Task 73; the
several-devices design, rulings 4.11, 4.12 and 5.2). An away device's
slices keep running, its own, their markers `ownerAway`. When its 3
minutes end, or it leaves with `session.leave`, its slices close and the
Core saves them for its return, with each slice's own settings; if no
other device is on the Core they keep running instead, held for it. A
token window's slices are not saved (it cannot be recognised again): with
another device on the Core they close, otherwise they pass to nobody.
Removing a device closes its slices, held ones included, and forgets what
was saved for it. The Core never closes its last slice for any of these;
that one stays, held (or owned by nobody). When a device is let in, the
Core returns the slices held for it, restores its saved slices where they
fit (a receiver window that covers each or a free receiver; its old
letter when free, else the lowest free), keeps any that do not fit for
next time, and, if it still owns none, gives it one (section 7.1).

**Routing with several devices** (iPhone app plan Task 72; the
several-devices design, ruling 5.8). Each session has its own view of the
station's state, with its own pending changes and its own connect-time
burst (section 5.1), each message fitted to what that session negotiated:
its minor and its capabilities, as before.

| Message | Goes to |
| --- | --- |
| `command.result`, `property.result`, `settings.reject` | the session that asked, only |
| `confirm.request`, `notice` | the one device they are for, only with `sessionHolderVersion` 1; a notice for an away device waits for its return (section 7.5) |
| `delta`, `object.create`, `object.destroy` | every session holding the object; a device's own write is not echoed to it (section 7.3). A `slice:<id>` only to the session of the device that owns it; a `marker:<id>` to every session with `sessionHolderVersion` 1 but that one (section 7.1) |
| `settings.value` | every session, with the writer's `origin` (section 8.1) |
| a write's readback of its side effects on the written object | the writer, only (section 7.3) |
| `capabilities` | each session its own, with its own share of the display budget (section 6.4) |
| `media.control`, media, `station.metrics.v1` | each session its own (section 11) |

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

## 14. Discovery

A station that listens makes itself known on its local networks two ways,
independent of each other (iPhone app plan Task 16; D36; the pairing
design, section 6): the LAN announcement, which desktops listen for, and
Bonjour, which the iPhone app browses for, because iOS lets an app receive
custom multicast only with a permission Apple grants on request. Both
follow one rule: they go out only on addresses the listener serves. A
listener bound to loopback only is neither announced nor advertised
(`stationLanListenerServesAddress` in `StationLanAnnouncer.cpp`,
`dnsSdInterfaceForListener` in `DnsSdAdvertiser.cpp`). Discovery is never
trust: a client pins what it finds (section 3.2) or pairs (section 3.6).

Both carry the same five facts about the Core, and both change when one
does (`DaemonApp::updateStationAnnouncement`):

- **identity**: the identity fingerprint, SHA-256 of the identity key
  (section 3.4);
- **label**: the Core's label as displayed (`StationLabel`, renamed by
  `station.rename`, section 9.1), or empty when the Core has none. A list
  shows the label, or the Core name when it is empty;
- **claimed**: whether the Core has a paired device or an active token
  (`DeviceStore::isClaimed`);
- **pairing**: how the Core takes a new device right now, from its pairing
  window (section 3.6): `click` while `OpenUnclaimed` with
  `pairing_lan_click` allowed (one tap on this network pairs; the code does
  too), `code` while `OpenUnclaimed` with it denied or while
  `OpenReopened`, and `closed` while `ClosedClaimed` or when the Core does
  not pair (`pairingVersion` 0);
- **devices** (iPhone app plan Task 71): how many devices hold a place on
  the Core, 0 to 4, counted as section 12.3 counts them
  (`StationServer::devicesConnectedForDiscovery`); a Core no device has
  claimed sends 0. A number only: who is on the Core reaches paired,
  signed-in devices alone (`connectedDevices`, section 7.1). A Core
  reached through the rendezvous or the relay has no announcement, so its
  count shows only after sign-in.

### 14.1 The LAN announcement

(`StationLanAnnouncement.h`, `StationLanAnnouncer.cpp`)

- UDP to port 47910, to the multicast groups 239.255.42.99 (IPv4) and
  `ff12::4e52:5344` (IPv6), from one source address of each family on each
  eligible interface, with a multicast hop limit of 1 and multicast
  loopback off;
- once when it starts, then every 5 s (`kStationLanAnnouncementIntervalMs`
  5000); a listener forgets a station it has not heard for 15 s
  (`kStationLanCacheTtlMs`);
- a listener takes datagrams of at most 512 bytes
  (`kStationLanMaxDatagramBytes`); with the fields below a schema-2
  datagram is at most 480 (`kStationLanMaxSchema2DatagramBytes`; 479 before
  the device count), so no field is ever cut short.

A station sends schema 2 only (`kStationLanAnnouncementSchema`). A listener
reads schema 1 and schema 2, so a Core from before schema 2 is still found.
The datagram is binary, in this order; schema 1 ends after the radio MAC:

| Field | Size | Value |
| --- | --- | --- |
| Magic | 4 bytes | ASCII `NRSC` |
| Schema | 1 byte | 1 or 2 |
| Service | 1 byte | 1: the control WebSocket over TLS (`kStationLanWssControlService`) |
| Control port | 2 bytes | big-endian, not 0 |
| Pin | 95 bytes | the certificate pin (section 3.2), uppercase |
| Core name length | 1 byte | 1 to 128 |
| Core name | that many bytes | UTF-8, no control characters: `core_name`, or the host name |
| Radio connected | 1 byte | 0 or 1 |
| Radio name length | 1 byte | 0 to 128; at least 1 when the radio is connected |
| Radio name | that many bytes | UTF-8, no control characters |
| Radio MAC | 17 bytes | uppercase hex pairs joined by colons; `00:00:00:00:00:00` only when no radio is connected |
| Claimed | 1 byte | schema 2: 0 or 1 |
| Identity | 32 bytes | schema 2: the identity fingerprint, raw |
| Label length | 1 byte | schema 2: 0 to 65 (`kStationLanMaxLabelBytes`) |
| Label | that many bytes | schema 2: ASCII letters, digits, `/`, `_` and `-` (a callsign of up to 32, `/`, a suffix of up to 32) |
| Pairing | 1 byte | schema 2: 0 `closed`, 1 `click`, 2 `code` |
| Devices connected | 1 byte | schema 2, appended by iPhone app plan Task 71: 0 to 4 (`kStationLanMaxDevicesConnected`), the places taken; a station always sends it. A reader that never sees it (a datagram from an older Core) takes the count as not known and shows none |

**Schema 2 extends by appending.** A reader ignores any bytes after the
schema-2 fields it knows. It still refuses a datagram that is too short
for those fields, or larger than 512 bytes, and applies every other rule
in this section. A writer only ever appends a new field after the last
one, and each new field states the value a reader that never sees it
assumes. Schema 1 does not extend: bytes after a schema-1 datagram's
radio MAC are refused.

A listener refuses a datagram with another magic, service or schema, a
field that fails these rules, or, in schema 1, any bytes left over. It dials
`wss://<source address>:<control port>`, with the IPv6 scope when the
address is link-local, and pins the announced pin. It keeps one entry per
endpoint (pin, source address and scope, interface, control port). A
schema-1 datagram for an endpoint that already sent schema 2 updates only
the fields schema 1 carries; it never clears the identity, label, claimed
state or pairing (`StationLanCache::ingest`).

A schema-1 datagram for an endpoint that already sent schema 2 does not
clear its device count either.

The conformance vectors `media/lan-announcement.bin` (schema 1),
`media/lan-announcement-2.bin` (schema 2, from a Core before the device
count) and `media/lan-announcement-2-devices.bin` (the same datagram with
the count, 2) (section 16.4) are datagrams the
station's own encoder wrote, with their decoded fields, `schema` among
them, in the `.expect.json` beside each (`devicesConnected` only where the
datagram carries it). `media/lan-announcement-2-trailing.bin` is the
`lan-announcement-2-devices` datagram with five bytes appended after the
count, as a later field would be; its expectation holds the same fields
and `ignoredTrailingBytes` 5. `tst_link_conformance_media`
decodes each and encodes the fields again, so a change to this layout
fails there until the vectors, and this table, move with it.

### 14.2 Bonjour

(`DnsSdAdvertiser.h`)

The station registers one DNS-SD service:

- service type `_nereus-station._tcp` (`kDnsSdServiceType`), domain
  `local.`, on the listener's port;
- on every interface for a listener on every address, or on the interface
  holding the address a listener is bound to; never for a loopback
  listener;
- instance name: the label, or the Core name when there is no label,
  without control characters and cut to 63 bytes of UTF-8 at a character
  boundary (`dnsSdInstanceName`), or `NereusSDR Core` when nothing is left.
  Bonjour renames it when another service holds the name, so a client reads
  the Core's label from the TXT record's `name`, not from the instance
  name;
- a TXT record of six entries, in this order:

| Key | Value |
| --- | --- |
| `v` | `1`, this record's version |
| `id` | the first 22 characters (`kDnsSdIdentityPrefixChars`) of the identity fingerprint in base64url without padding (RFC 4648 section 5) |
| `claimed` | `0` or `1` |
| `pair` | `click`, `code` or `closed` |
| `name` | the label, possibly empty; at most 65 characters |
| `devices` | iPhone app plan Task 71: `0` to `4`, how many devices hold a place on the Core (section 14); `0` on a Core no device has claimed. `v` stays `1`: an older client ignores the key |

A client ignores a key it does not know, so a newer station still lists,
and treats a record whose `v` is not `1` as one it cannot read. `id` names
the Core in a list; it proves nothing, and a client that pairs or signs in
checks the whole identity key the station's `hello` carries (section 3.5).

The station updates the TXT record in place when a fact changes, and
registers again when the instance name changes. Each platform uses what it
ships: `dns_sd.h` on macOS (`DnsSdAdvertiserApple.cpp`), the Avahi daemon
over D-Bus on Linux (`DnsSdAdvertiserAvahi.cpp`, with Qt's D-Bus module),
and `DnsServiceRegister` from `dnsapi.dll` on Windows 10 1903 and later
(`DnsSdAdvertiserWindows.cpp`). Where Bonjour is not available (no
avahi-daemon, an older Windows), the station still announces over the LAN
datagram and logs once that iPhones and iPads will not find it by
themselves (`DnsSdAdvertiser::unavailableText`).

The conformance vector `media/dnssd-txt.bin` (section 16.4) is the TXT
record the station's encoder writes for the Core of
`media/lan-announcement-2-devices.bin`, each entry preceded by its length in one
byte (RFC 6763 section 6.1), with the service type and the entries as
strings in `media/dnssd-txt.expect.json`.

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
| `confirmExpiryMs` | 60000 | ms | ConfirmStep::kExpiryMs |
| `connectDeadlineMs` | 30000 | ms | kStationHandshakeDeadlineMs |
| `deltaFlushMs` | 50 | ms | StationServer::kDefaultDeltaFlushMs |
| `endpointFps` | 1 to 60 | frames per second | DaemonMediaController.cpp handleSubscribe literal 1; kMaximumSpectrumDisplayFramesPerSecond |
| `endpointPixels` | 1 to 4096 | pixels | DaemonMediaController.cpp handleSubscribe literal 1; SpectrumEndpoint::kMaxPixels |
| `graceMs` | 180000 | ms | DeviceSessionRegistry::kGraceMs |
| `heartbeatIntervalMs` | 20000 | ms | StationServer::kDefaultHeartbeatIntervalMs |
| `lanAnnouncementMaxBytes` | 480 | bytes | kStationLanMaxSchema2DatagramBytes |
| `maxDeviceSessions` | 4 | count | StationServer::kMaxDeviceSessions |
| `maxDisplayEndpoints` | 8 | count | DaemonMediaController.cpp kMaxEndpoints |
| `maxHandshakesPerAddress` | 2 | count | StationServer::kMaxHandshakesPerAddress |
| `maxPeers` | 24 | count | StationServer::kMaxConcurrentPeers |
| `mediaControlBytes` | 131072 | bytes | kMaxMediaControlBytes |
| `missedPongs` | 2 | count | StationServer::kDefaultMaxMissedPongs |
| `shortNameMaxBytes` | 32 | bytes | DeviceStore::kMaxShortNameBytes |
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
  `<codec>` is `nsdc1`, `nsdx1` (the display extras datagram), `ps3d`,
  `opus`, `nrsc1` (the LAN announcement of section 14.1) or `dnssd-txt`
  (the Bonjour TXT record of section 14.2). `expect` may hold `"after": ["<fixture id>", ...]` for a
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
| `"$ref:device:<n>"`, `"$ref:device:self"` | only in a fixture for the station alone: the id (base64url key fingerprint) of a device the station runner paired at run time, `<n>` from 1 to `otherPairedDevices` in the order it paired them, `self` its own (`pairedDevice`); recorded before the client connects | yes | yes, by the station's runner |
| `"$within:<t>:<v>"` | a number no further than `<t>` from `<v>`; `<t>` and `<v>` are each exactly a JSON number (RFC 8259 section 6: no `+`, no leading `.`, no `inf` or `nan`, no spaces), `<t>` at least 0; any other text is a malformed fixture | yes | no |
| `"$device:<case>"` | only as `auth.request`'s `device` | no | yes, by the station's runner: the device block (section 3.5, with `shortName` "Conformance") of the runner's own device, whose key it makes at run time, signing the transcript of the challenge recorded as `challenge`; `<case>` is `signed` (that transcript), `otherChallenge` (a challenge of the runner's own in its place) or `otherCertificate` (another certificate's SHA-256 in place of the station's). An app's runner fills none: in a fixture for the app only `"$device:signed"` appears, in a behaviour step, and it is checked (section 16.3) |

A number the station's DSP measures is written `"$within:<t>:<v>"`, with
the tolerance stated, never `"$any"`; a counter whose value depends on
timing is `"$int"`, not pinned.

**JSON inside a string.** Where the station sends a string that holds
JSON (a `utf8` property such as `connectedDevices`' `listJson`), a
fixture may write `{"$json": <expectation>}` in the string's place: an
object whose one key is `"$json"`. It matches a string that parses as
exactly one JSON value (RFC 8259: an object, an array, a string, a
number, `true`, `false` or `null`, with whitespace around it allowed),
and that value must match `<expectation>` by this section's rules: the
placeholders above stand inside it, a `{"$json": ...}` may nest inside
it, object keys match in any order and arrays in order and length. A
name recorded inside it is recorded for the whole fixture run, so a
`"$ref:<name>"` inside or outside a string refers to it. A value that is
not a string fails as a wrong type; a string that does not parse, or
holds nothing or more than one value, fails with the fixture, the step,
the path and "not JSON" (the station's runner reports
`<fixture>: step <n> (station <type>): <path>: not JSON (<why>), got
<the string>`); an object holding `"$json"` beside another key is a
malformed fixture. The path inside the string is the string's path
followed by `($json)`, as `$.properties[0].value($json)[1].state`.
`{"$json": ...}` stands only in a station message: in a client message
it is a malformed fixture, and neither runner fills one there. In a
fixture for the app, its expectation holds only what a station message
there may hold (section 16.3), and an app's runner sends it to its
client as the string of the expectation filled as that section fills a
station message, written as compact JSON (no whitespace, keys in any
order).

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
travels: eleven from the client (`hello`, `auth.request`, `command.invoke`,
`media.control`, `property.write`, `settings.write`, `settings.remove`,
and `pair.start`, `pair.spake`, `pair.confirm`, `pair.fail`) and
twenty-two from the station (the sixteen before pairing, `pair.accept`,
`pair.spake`, `pair.confirm`, `pair.fail`, and `confirm.request` and
`notice`, section 7.5), with a `delta` carrying `"nan"` and `"-inf"`
(section 4.2). The client's `hello` has two fixtures: an older app's,
without `majors` or `features`, and one declaring both; `auth.request` has
three: a token, a device sign-in with its `device` block (section 3.5), and
one whose block carries `shortName`.
The station's `hello` carries `majors`, `features` (`deviceAuth` 1 and
`pairing` 1),
`identity` and `challenge`, from a station supporting `[1, 2]`, so `major`
is 1, the oldest (section 6.1). `auth.result` and `session.end` each have a
second fixture carrying a `code` (section 12.4). The refusals are: a
`media.control` over its 128 KiB cap and a `station.metrics.v1` over its
16 KiB cap (each an otherwise valid message padded past the cap), a
missing required key (`command.invoke` without `id`), a wrong type (an
`f64` entry holding `true`), an `f64` string other than the three of
section 4.2, a `hello` major above 65535, a `hello` with an empty `majors`,
a `hello` declaring a feature version that is not a whole number, a
`hello` whose `identity` is not an object, an `auth.request` whose `device`
lacks `signature`, one whose `shortName` is not a string, a `session.end` with an empty `code`, a `pair.start`
whose `mode` is neither `lan` nor `code`, a `pair.spake` step outside 0 to
3, a `pair.fail` whose `retryAfterMs` is not a whole number, a `notice`
whose `takeBack` is not a boolean, and an
unknown `type`. The pairing fixtures carry placeholders for keys, shares
and boxes; the live exchange is proved by `nereus_pairing_peer` (section
3.6).

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

**Several clients** (iPhone app plan Task 71). A fixture may play other
clients beside its own, each signing in as a paired device:
`stationSetup.otherClients` is `[{"name": "b", "device": 1, "features":
{...}, "shortName": "..."}]`, where `device` is `n` (the paired device
`"$ref:device:<n>"` names, so `otherPairedDevices` pairs at least `n`) or
`"self"` (the runner's own device), `features` is what that client's
`hello` declares and `shortName`, when given, what its sign-in carries.
A client step may carry `"client": "<name>"` and a station step
`"to": "<name>"`; absent means the fixture's own client. Two more steps:
`{"connect": "<name>"}` runs that client's whole connect sequence (its
`hello`, its sign-in, and its messages up to `snapshot.complete`, taken
without matching), and `{"close": "<name>"}` closes its connection;
`expectClosed` may carry `"client"` too. The station's messages are
matched per client, in that client's own arrival order. `otherConnections`
stays for sockets that never sign in. **An app's runner** plays only its
own client: it skips other clients' steps and the station messages sent
to them, and a `connect` or `close` step names nothing it plays. A fixture
where the own client shares the Core names the features its `hello`
declares as a literal (`{"deviceAuth": 1, "sessionHolder": 1}`), not
`"$object"`, since what the station sends it depends on them; an app's
client declares them.

**The radio's own PTT** (iPhone app plan Task 77). A step `{"radioPtt":
true}` or `{"radioPtt": false}` sets the level of the station radio's own
PTT input (its microphone or a footswitch), as its status frames report it;
the station's runner presses or releases it and lets the station act. A
fixture with such a step runs on the station alone (`"runs":
["station"]`); an app has no radio to press. `stationSetup.unkeyWalkMs`
(0 to 5000, with `transmitReady`) makes the radio's walk from transmit
back to receive take that many real milliseconds, so a transfer that
unkeys a holder is still running when the next message arrives. A verb
whose arguments are all optional (`tx.take`) may be sent with none.

**Which fixtures run on the app.** A fixture whose client behaviour no
app can adopt runs on the station only (`"runs": ["station"]`): an older
app's `hello` (`major-refused`, `lower-minor`), made-up majors or features
(`version-*`), a client that answers no ping (`heartbeat-missed`) or never
sends its token (`connect-deadline`), the lockout, which needs other
clients (`lockout`), an older window meeting a full Core (`older-window`)
and a device's own connection replaced (`same-device-again`), and the device proofs
that fail (`device-other-challenge`, `device-other-certificate`), which
hold the Core's verification to a block a conformant client never sends.
Their client steps are all `scripted`. The device sign-in fixtures where
the app is the thing under test run on both ends: `device-sign-in` (the
app's device signs this connection's transcript and is admitted),
`device-not-paired` (the app's well-formed sign-in is refused
`deviceNotPaired` and the app handles the refusal and its code) and
`pairing-required` (the app's token sign-in is refused `pairingRequired`,
handled the same way). `tst_link_conformance_session` checks that a fixture
marked for the app holds nothing a conformant client could not send, and
nothing an app's runner could not send its client:

- its `hello` and `auth.request` are behaviour: the `hello` has `majors`
  `"$majors"` (an app supporting its own major and the one before sends
  both, section 6.1) and `features` `"$object"`, with `peer`
  `"$string"` and `settingsSchema` `"$int"`; the token is `"$ref:token"`
  or, for a refused token, `"$string"`; a device sign-in is `token` `""`
  with `device` `"$device:signed"`, after a station `hello` whose
  `challenge` is `"$capture:challenge"`;
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
- a station message holds no `"$capture:<name>"` (except the station
  `hello`'s `challenge` as `"$capture:challenge"`), `"$string:<name>"`,
  `"$int:<name>"` (ranged or not), `"$object"` or `"$majors"`, and
  `"$any"` only as a summarised snapshot (below); what an app's runner
  sends for each placeholder a station message may hold is defined
  below.

**Filling a station message (an app's runner).** An app's runner sends
its client each station message with `"$string"` as `""`, `"$int"` as `0`,
`"$ref:<name>"` as the recorded value, `"$within:<t>:<v>"` as `<v>` and
`{"$json": <expectation>}` as the compact JSON text of `<expectation>`
filled by these same rules (section 16.1),
except in the station `hello`:

- `identity` is a test station identity the runner makes at run time (a
  P-256 key, section 3.4): `publicKey` is its key and `certBinding` its
  binding of the certificate SHA-256 the runner's transport reports to its
  client for this connection, so a client that checks the binding before
  signing finds it good.
- `challenge` is 32 random bytes of the runner's own, base64url, new for
  each run; written `"$capture:challenge"`, it is also recorded as
  `challenge`.

**The app's device (an app's runner).** In a behaviour `auth.request`,
`"$device:signed"` is a check, not a fill: the block the app's client
sent must be well formed (section 3.5; a `shortName`, when present, a
usable short name), its `id` the fingerprint of its
`publicKey`, a canonical P-256 key, and its `signature` must verify over
this connection's transcript: the challenge recorded as `challenge`, the
certificate SHA-256 the runner's transport reported, the test station
identity's key and the block's own key. The runner accepts the app's key
as paired: `stationSetup`'s `pairedDevice`, like the rest of
`stationSetup`, means nothing to it, and the station messages that follow
say whether the sign-in was admitted. For the station's runner,
`"$device:<case>"` keeps its fill (section 16.1), whatever the step's
role.

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
- **Summarised snapshots.** Outside `connect-connectable` and the two
  catalogue fixtures, whose `catalog` object is in full (and where a
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
| `board` | the static radio's model: `"hermesLite2"`, a Hermes Lite 2 on Protocol 1, or `"ananG2"`, an ANAN-G2 on Protocol 2 (same MAC); only with `"radio": "static"` | `"hermesLite2"` |
| `radio` | `"static"`: a model reporting a connected Hermes Lite 2 (MAC `AA:BB:CC:DD:EE:01`, or the `board` below) with no radio behind it, so nothing changes on its own; `"connectable"`: a model connected to the fake Protocol 1 radio, receive processing and all; a receive-only station permits PureSignal (`transmitSettingsVersion` 7), so the runner waits for PureSignal's readiness (`canActuate`) to come on, and it waits for each slice's signal readings to leave the no-reading value before the client attaches | `"static"` |
| `slices` | slices before the client connects | 1 |
| `panadapters` | panadapters before the client connects | 0 |
| `coreAccessories` | the Core owns its accessories: the `tuner`, `amplifier`, `rfkit`, `accessoryData` and `accessorySettings` objects and the accessory commands | false |
| `stationTci` | the Core runs a station TCI server (it stays off) | false |
| `stepAttenuator` | a step attenuator controller is bound, so the radio hardware objects are offered | false |
| `media` | media is enabled | false |
| `priorFailedAuthentications` | other clients that each sent a wrong token before this one connects | 0 |
| `clientAnswersPings` | the client's transport answers the station's pings | true |
| `otherClients` | other clients the runner plays beside its own, each signing in as a paired device (above) | none |
| `otherConnections` | other clients connected before this one, still connecting and sending nothing; 24 puts the station at its connection limit | 0 |
| `lanScanWindowMs` | how long the Core's `scanTgxlLan` and `scanPgxlLan` listen, in milliseconds from 1; the scan fixtures set 150, since their answer is matched as any text | 3000 |
| `token` | `"active"`: a Core upgraded from before paired devices, with a pairing token (made at run time) that `"$ref:token"` names; `"none"`: a new Core, without one (section 3.3) | `"active"` |
| `receivers` | iPhone app plan Task 74: the static radio's receivers, 192 kHz wide each, sized before its slices are made, so slices bind to them and the rules of section 7.5 apply; only with `"radio": "static"` | none |
| `maxSlices` | with `receivers`, the slice cap | 5 |
| `alexRxAntennas` | iPhone app plan Task 75: the static radio's receive antenna per band, 14 numbers 1 to 3 in `Band` order (160 m to XVTR), and band tracking on (the per-band antenna switch of section 7.6) as though a radio were connected; only with `"radio": "static"` | none (no band tracking) |
| `otherPairedDevices` | that many devices besides the runner's own are paired before the client connects, their keys made at run time and never written in a fixture; their ids are `"$ref:device:1"` onwards; an app's runner ignores it | 0 |
| `pairedDevice` | the station runner's own device (its key made at run time, the one `"$device:<case>"` signs with) is paired with the station before the client connects; an app's runner ignores it, as it ignores all of `stationSetup`, and accepts its app's key | false |

The station runner starts every fixture from an empty settings profile,
and the bundled NR3 model files count as absent, so a fixture reads the
same on every machine.

| Fixture | What it holds the station to |
| --- | --- |
| `connect-connectable` | The whole connect sequence to `snapshot.complete` on a connected radio with one slice, every message in full, except: PureSignal's `statusJson` (`"$string"`, it carries a capture time) and `displayGeneration` (`"$int"`, a counter whose value depends on timing), the catalogue's `json` (`"$string"`, held in full by the catalogue fixtures), and the slice's `signalStrengthDbm`, `signalPeakDbm` and `signalAverageDbm`, which the receiver measures: `"$within:0.5:-399.02"`, within 0.5 dB, which leaves out the meter's no-reading value of -400 |
| `wrong-token` | `auth.result` refused, `retryable` false, `code` `wrongToken`, then the close |
| `pairing-required` | On a new Core (no token), a token sign-in is refused "This Core uses paired devices. Pair this device first.", `retryable` false, `code` `pairingRequired`; on the app, the app handles that refusal and does not reconnect |
| `device-sign-in` | On a new Core, the paired device signs this connection's transcript (`"$capture:challenge"` from the `hello`, then `"$device:signed"`) and is admitted: the whole connect sequence to `snapshot.complete`, summarised; on the app, the app's own device signs and its block is checked |
| `device-not-paired` | A well-formed device sign-in from a key the Core has not paired: `code` `deviceNotPaired`, then the close; on the app, the app's device signs, its block is checked, and the app handles the refusal |
| `device-other-challenge`, `device-other-certificate` | The paired device signs another challenge, or another certificate: `code` `deviceProofFailed`, then the close |
| `devices` | On a new Core with the runner's device and two others paired, a device that declares `deviceAuth` receives the `devices` object in its snapshot (`listJson` and `keyPath` as `"$string"`, since they carry run-time ids, pairing and sign-in times and a path); a rename with a renamed argument and with a label outside the rule is refused, then a rename is stored (`settings.value` `StationLabel`) and the object's next `delta` carries it; raw `settings.write` and `settings.remove` of `StationLabel` are refused; the key backup is acknowledged; another device is revoked; `station.retireToken` with no token is accepted; then the device revokes itself: `command.result`, `session.end` `deviceRemoved`, the close. Runs on the station alone |
| `devices-not-offered` | A window at minor 11 that declares no features receives no `devices` object, and `station.rename` is refused "Update this app to manage this Core's paired devices." Runs on the station alone |
| `devices-pairing` | On the same Core as `devices`, `pairing.open` and `pairing.close` each with a renamed argument are refused; `pairing.open` is accepted with `values` `code` as `"$string"`, and the object's next `delta` has `pairingWindowOpen` true and `pairingCode` `"$string"`; `pairing.close` is accepted and the next `delta` has them false and `""`. Runs on the station alone |
| `devices-retire-token-refused`, `devices-retire-token` | On an upgraded Core, a token connection that declares `deviceAuth` receives the object with `tokenActive` true; `station.retireToken` is refused with no device paired, and with one paired it is accepted and the connection ends: `session.end` `pairingRequired`, `retryable` false. Run on the station alone |
| `catalog-anan-g2`, `catalog-hermes-lite-2` | The connect sequence to `snapshot.complete` on the static radio as an ANAN-G2 and as a Hermes Lite 2: the capabilities in full, and the `catalog` object with its `json` in full and `revision` 1 (section 7.4). The two differ exactly where the radios do: the board's model, name, attenuator (0 to 31 against -28 to 31), sample rates (six against four), antennas (three plus three receive-only against one plus none), PA rating and microphone input, and the RF power gauge its rating scales |
| `connection-limit` | With twenty-four other connections still connecting, the station sends no `hello`: `session.end` "The Core already has as many connections as it allows. Try again shortly.", `retryable` true, then the close |
| `lockout` | After five wrong tokens from other clients, the right token is refused as rate limited, `retryable` true |
| `major-refused` | An older app's `hello` (no `majors`) with major 2 gets `session.end` "This Core runs link version 1 and this app runs version 2. Update the Core.", `retryable` false, `code` `linkVersion` |
| `version-declares` | A `hello` with `majors` `[1]` and a declared feature is accepted, and authentication follows |
| `version-app-one-ahead` | An app supporting `[1, 2]` chooses 1, the highest it shares with the station, and is accepted |
| `version-app-two-ahead` | An app supporting `[2, 3]` that sends major 3 gets `session.end` "This Core runs link version 1 and this app runs version 3. Update the Core.", `retryable` false, `code` `linkVersion` |
| `lower-minor` | A `hello` with minor 4 agrees minor 4: the capabilities without the minor-11 entries, and a minor-11 verb refused with a plain reason |
| `same-device-again` | The device signs in again on another connection (`otherClients` `"self"`): the older connection ends with `session.end` "This device connected again.", `retryable` false, `code` `sameDevice`, and the newer one is let in with no question. Runs on the station alone |
| `older-window` | A window that signs in by key but predates several devices is let in first and owns the Core's slice; when a device with the feature is let in with a slice of its own, the older window is sent no marker for it, only the `devices` list moving. Four devices then fill the Core; a window from before paired devices (the token, no features) is let through `auth.result` and then turned away: `session.end` "The Core is full. Update NereusSDR to take a device's place, or try again later.", `retryable` true, no code. Runs on the station alone |
| `connected-devices` | A device that declares `sessionHolder` receives `connectedDevices` in its snapshot (`deviceLimit` 4, `revision` 1), and a `delta` of it (with one of `devices`) each time another device is let in, including a window that declares only `deviceAuth` and so never receives the object itself, and when one drops; each device let in gets a slice of its own, which reaches this device as a marker; `listJson` is a `{"$json": ...}` of the list's shape (section 16.1): each entry's keys with its literal name, short name, kind, flags, state and `listeningOn` (`{sliceId, letter, band, mode}`), its `deviceId` `"$string"` and its three durations `"$int"`, since ids are made at run time and a fixture for the app holds no capture |
| `short-name` | Another device signs in with a short name, drops, and signs in again with a new one: each change moves `connectedDevices`' and `devices`' revisions, the new short name replacing the old in both lists and on its slice's marker |
| `grace-return` | Another device drops and is away; a minute later nothing has been sent about it but its slice's marker turning `ownerAway`; it signs in again within its 3 minutes and is let in with no question, the list sent again and its marker back; it drops again, and when its 3 minutes end with this device still on the Core its slice closes (the marker's `object.destroy`) and is saved for its return |
| `two-devices` | Another device holds the Core's slice; this device is let in with a slice of its own (`slice:1`) and the other's as `marker:0`, naming its owner, with the `SliceMarker` schema; the other device is sent `marker:1` for this device's slice, and when this device tunes its slice the other sees the marker move, never the slice |
| `foreign-write-refused` | This device holds slice 0 and another device slice 1: a `property.write` to `slice:1` and to `marker:1`, and `removeSlice` and `setActiveSliceById` naming slice 1, are refused "That slice belongs to Other device 1. It can be changed only there.", with no value sent back and nothing changed; its own slice it may make active |
| `held-for-device` | Another device, alone on the Core, leaves with `session.leave`: its slice keeps running, held for it. This device, let in meanwhile, does not adopt it: it gets a slice of its own and the other's as a marker with `ownerAway` true; the other device signs in again and the marker's `ownerAway` turns false (the slice is its own again) |
| `verbs-tx-set-tx-slice` | On a Core with `remote_transmit` allow (stationSetup `remoteTransmit`), a device that declares `remoteTx` is sent `txPermitted` false in its first `capabilities` and true in the `capabilities` sent again after `snapshot.complete`, each with `remoteTxVersion` 1 and the `txRefusal` entries (`notReady` first, empty once permitted); `tx.setTxSlice` with an argument it does not take is refused "The Core could not read this request."; with `sliceId` while nobody holds transmit it is refused "Take transmit on this device first." with the values `refusalCode` `notHolder` and `refusalFix` `takeTransmit`. Runs on the station alone |
| `unheld-key` | On a Core with `remote_transmit` allow whose radio can key (stationSetup `transmitReady`) and whose device's media carries a microphone line (stationSetup `microphoneLine`), one device that declares `remoteTx`: each keying verb with an argument it does not take is refused "The Core could not read this request."; a program's `tx.key {trigger:"tci"}` on unheld transmit is refused `programNeedsTransmit` and nobody takes transmit; a person's `tx.key {trigger:"screen"}` takes it and keys (epoch 1; `transmitting` true, the device's entry transmitting); `tx.unkey {epoch:1}` unkeys; the same program's key then keys (epoch 2) and `tx.unkey {epoch:2}` unkeys; `tx.tune {on:true}` tunes (epoch 3, `transmit`'s `tune` true) and `{on:false}` ends it; `tx.twoTone {on:true}` on a radio with no transmit channel is refused "The two-tone test could not start on the Core." Each key and unkey also moves `txState` (section 18.8): `keyed`, who keyed and how (`keyedTrigger` `screen`, `tci`, `tune`), `keyedSinceMs` on the runner's virtual clock and 180 s left for the phone. Runs on the station and the app |
| `key-without-microphone` | On the same Core as `unheld-key` but with no microphone line for the device (stationSetup `microphoneLine` absent): a person's `tx.key {trigger:"screen"}` is refused `micNotReady`, "This device's microphone is not connected to the Core yet. Wait a moment and try again.", and nothing keys; a program's key on unheld transmit is still refused `programNeedsTransmit` first. Runs on the station and the app |
| `tx-keepalive` | On the same Core as `unheld-key`: `tx.keepalive` with an argument it does not take is refused "The Core could not read this request."; a keepalive while nothing of the device's is watched is accepted and changes nothing; the device keys (epoch 1) and sends keepalives 50 ms after the key and then 300 ms apart (sequences 2 to 4, epoch 1), each accepted, and the key stays on; then none for 450 ms: the watchdog stops transmitting (`transmitting` false, the device's entry no longer transmitting). `txState` follows the key (180 s left, 179 a second later), and the watchdog's stop is its stop: `stopReason` `linkLost`, `stopSerial` 1, "The link to Conformance device went quiet, so the Core stopped transmitting." Runs on the station and the app |
| `grace-transmit-held` | Two devices that declare `remoteTx`: the other device keys and this one's permission goes false (`capabilities` sent again) and its `tx.key` is refused "Other device 1 has the transmitter."; the holder's link drops: this device is sent `capabilities` again with the refusal "Transmit is changing hands. Try again in a moment." and then "Other device 1 has the transmitter.", the Core stops transmitting at once (`transmitting` false) and the holder, away, still holds transmit (this device's `tx.key` is refused naming it); the holder signs in again a minute later, nothing keys until its own `tx.key` (epoch 2), and its `tx.unkey {epoch:2}` unkeys. `txState` follows each key, and the holder's dropped link is its stop: `stopReason` `linkLost`, `stopSerial` 1, "The link to Other device 1 went quiet, so the Core stopped transmitting." Runs on the station and the app |
| `on-air-refusals` | While another device (short name "Tablet B") is keyed, this device's Protocol 1 rate change, `ps3.off`, its slice's `rxAntenna` and `transmit`'s `pureSig` are each refused "Tablet B is on the air. Try again when they stop." (commands with `refusalCode` `holderOnAir` and `refusalFix` `takeTransmit`). Runs on the station alone |
| `take-transmit` | Two devices that declare `remoteTx` and `sessionHolder` (Task 77): the other device's `tx.take` on unheld transmit is taken at once (`holderEpoch` 1) and the transmit flag moves to its own slice (this device's `slice:0` `txSlice` false, the other's marker true); this device's `tx.take {}` is refused "Waiting for you to confirm." with `phase` `needsConfirmation` and a `confirm.request` `takeTransmit` whose `holder` names "Other device 1", `state` `listening`, `keyed` false; `confirm.proceed` takes it (`holderEpoch` 2, the flag back on `slice:0`) and the other device gets `transmitTaken` "Conformance device took transmit." with Take it back; this device's `tx.take` again is accepted and changes nothing; the other device's `tx.take {holderEpoch:2, shownKeyed:false}` takes at once. Runs on the station and the app |
| `take-transmit-keyed` | The other device takes transmit; this device's `tx.take` is asked (holder `listening`); the other device keys; this device's proceed is then refused "Waiting for you to confirm." and asked again, red (`state` `transmitting`, `keyed` true); the second proceed unkeys the other device before this one holds, unkeyed (`holderEpoch` 2, `transmitting` false, `stopReason` `takenOver`, "Conformance device took transmit, so the Core stopped transmitting."), and the other device is told. Runs on the station and the app |
| `transfer-refuses-keys` | With the unkey walk slowed (stationSetup `unkeyWalkMs` 300), the other device keys; this device's `tx.take {holderEpoch:1, shownKeyed:true}` starts the transfer; the other device's `tx.key` during it is refused `changingHands` "Transmit is changing hands. Try again in a moment."; the take's answer arrives when the transfer ends (`holderEpoch` 2). Runs on the station alone |
| `radio-ptt-takes-transmit` | This device keys; the radio's own PTT is pressed (step `radioPtt` true): transmit is taken without a question, this device is told (`transmitTaken`, `byName` "Radio", `bySource` `radioPtt`, `byKind` `station`), every device's `txState` names "Radio" with `holderSource` `radioPtt` and `stopReason` `takenOver`, and this device's `slice:0` shows `txSlice` false while the radio transmits on it; this device's `tx.take` is asked red (holder `source` `radioPtt`, `state` `transmitting`) and taken back; the PTT still held (its level again) takes nothing; this device keys (epoch 3) and the PTT's release unkeys nothing. Runs on the station alone |
| `tx-mark` | `txSlice` is true only on the holder's own transmit slice, on `slice:` and `marker:` alike: this device takes transmit (its `slice:0` true, the other's view of `marker:0` true); the other device takes it at once as shown (`tx.take {holderEpoch:1, shownKeyed:false}`) and each view hears the changed values (`slice:0` and `marker:0` false, the other's `slice:1` and this device's `marker:1` true). Runs on the station and the app |
| `take-during-grace` | The other device takes transmit and its link closes; 40 s later this device's `tx.take` is asked without red (holder `state` `away`, `awayForSeconds` 40, `keyed` false) and the proceed takes it, nothing to unkey; the other device signs in again 20 s later (client `back`), does not hold transmit, and its `transmitTaken` notice (`secondsAgo` 20, Take it back) follows its `snapshot.complete`. Runs on the station and the app |
| `unheld-key-carriers` | On unheld transmit, `ps3.twoTone {enabled:true}` on a radio with no transmit channel is refused "The two-tone test could not start on the Core." and `tx.tunerTune {on:true}` with no Tuner Genius "No Tuner Genius is connected to the Core.", and neither takes transmit (this device's `tx.key` then takes it at `holderEpoch` 1); `tx.take` and `tx.tunerTune` with an argument they do not take are refused "The Core could not read this request." Runs on the station and the app |
| `share-receiver` | Another device's slice shares this device's receiver (this device anchors it); a C-Tune move that would leave it outside is answered "Waiting for you to confirm." with `phase` `needsConfirmation`, then a `confirm.request` `panMove` naming the other device and its slice's effect `moves`; `confirm.proceed` with a renamed argument is refused, with its own arguments it is accepted; the other device's slice moves to the free receiver and it is told (`notice` `sliceMoved`, no Take it back) |
| `anchor-band-change` | The same two devices; this device retunes its slice from 20 m to 40 m: the write is answered "Waiting for you to confirm.", then `panMove` with `change` "Receiver 1", "20 m", "40 m"; `confirm.cancel` with a renamed argument is refused, with its own it changes nothing; the write again, `confirm.proceed`: its result carries `objectKey` and `frequency`, the receiver follows this device's slice and the other device's slice moves, the other device told |
| `non-anchor-pan-move` | The other device moves its panadapter on this device's receiver while this device holds the second receiver: refused, naming this device, and asked to take one (`takeReceiver`); it cancels; this device removes its second slice and the other device's move goes to the free receiver, nobody asked |
| `take-receiver` | Each device holds one receiver; this device's `addSliceOnPan` is refused naming the other and asked `takeReceiver` (its own receiver `takeable` false); proceed takes the other's receiver: the other device's slice closes and it is told `receiverTaken` with Take it back; `notice.takeBack` with a renamed argument is refused; with its own it asks the other way, and proceed closes this device's new slice (told `receiverTaken`) and restores the other's |
| `take-slice` | The slice cap (2) full with a receiver free: `addSlice` is refused and asked `takeSlice`; proceed closes the other device's slice, which is told `sliceTaken`. Runs on the station alone |
| `grace-expired` | Another device drops; after its 180 s its slice closes; it signs in again and `graceEnded` follows its `snapshot.complete`, `secondsAgo` from when its time ran out |
| `older-window-taken-over` | A window without the feature holds the second receiver; this device takes it: the window's session ends `takenOver`, not retryable. Runs on the station alone |
| `older-window-no-slice` | The slice cap full, a window without the feature signs in: `session.end` "All the radio's slices are in use. Try again when another device closes one.", retryable. Runs on the station alone |
| `shared-setting-confirm` | iPhone app plan Task 75: another device's slice on the ADC this device's preamp feeds: the `stepAtt` write is answered "Waiting for you to confirm." with the Core's value, then a `confirm.request` `sharedSetting` with `change` "Preamp, ADC 1", "Off", "On" and `affected` naming the other device (`state` `listening`) and its slice's `mode`, `adc`, `streamIndex` and effect `changes`; `confirm.proceed` carries the readback (`objectKey`, `preampMode`), the other device is told (`notice` `settingChanged` with who, `change` and `secondsAgo`, no Take it back) and sent the `delta` |
| `confirm-grew` | The same question; before it is answered the other device opens a second slice on the ADC: `confirm.proceed` is answered "Waiting for you to confirm." with `phase` `needsConfirmation` and a new `confirm.request` naming both its slices, nothing applied; `confirm.cancel` of the new one. Runs on the station alone |
| `confirm-target-changed` | The same question; the other device changes the same preamp (asked, it goes ahead, this device is told `settingChanged`); this device's `confirm.proceed` is refused "That setting changed since you asked. Make the change again.", nothing more applied. Runs on the station alone |
| `older-window-shared-setting` | A window without the feature changes the preamp while a device with the feature listens on the ADC: refused "This change would affect Other device 1. Update NereusSDR to confirm changes that affect other devices.", nothing applied, never asked. Runs on the station alone |
| `antenna-kept` | An ANAN-G2 with ANT2 on 40 m and ANT3 on 80 m (`alexRxAntennas`); the other device's slice listens on the ADC on another receiver; this device tunes from 20 m to 40 m: the tuning goes ahead and this device is told `antennaKept`, "The antenna stays on ANT1 while Tablet B listens on it.", no `by` keys; the other device leaves, its slice closes, and this device's next crossing (to 80 m) switches the antenna (its slice's `rxAntenna` becomes ANT3), with no notice. Runs on the station alone |
| `verbs-session-leave` | `session.leave` with an argument is refused, "The request to leave the Core was not understood."; without, it is accepted and the station closes the connection with no `session.end`. Runs on the station alone |
| `heartbeat-answered`, `heartbeat-missed` | The heartbeat, above |
| `connect-deadline` | No `auth.request` within 30000 ms: `session.end` "This app did not finish connecting to the Core in time.", `retryable` true |
| `property-write` | A write and its `property.result` and side-effect `delta`; a refused outbound property and an unknown one; a write without a `writeId` answered by `delta`; a write to a slice's signal strength refused as outbound; on the receive-only Core, a `transmit` write of `power` taken off the air and a write of `mox` and `voxEnabled` refused with the receive-only reason; at `transmitSettingsVersion` 2, a write of `cpdrLevelDb` taken, and `micGainDb` and `monitorVolume` out of range refused with their ranges beside a write of the outbound `tunePowerForTxBand`; at `transmitSettingsVersion` 3, a write of `micBoost` and `lineInBoost` taken, and `lineInBoost` out of range refused with its range beside a write of the outbound `activeTxProfile`; at `transmitSettingsVersion` 4, a write of `txEqBandsJson`, `txEqUseLegacy` and `txLevelerDecay` taken, and a nine-value `txEqBandsJson`, a `cfcCompressionJson` with a value out of range and `txAlcDecay` out of range each refused whole with its range |
| `settings-write` | A station-scoped write echoed with its origin; an operator-local write rejected; a removal sent as `settings.value` with no entry; on the receive-only Core, a DSP > Options TX key and a PA forward-power table key (`paCalibration/calPoint1`, version 6) taken off the air, an OC transmit pin (`oc/tx/20m/pin3`), an OC pin action (`oc/actions/pin1/action`) and TX Display Cal (`cal/txDisplayOffset`) taken off the air (version 8), and a transmit hardware key refused |
| `unknown-verb` | `command.result` refused, "The Core does not know this request. Updating the Core may help."; the connection stays up |
| `unknown-kind` | `session.end` "The Core could not read a message from this app.", `retryable` false, `code` `protocolError` |
| `verbs-*` | Each verb in `commands`, grouped by the capability that gates it, invoked with its own arguments (for `setPgxlHardware`, one of its three optional ones) and, where it takes any, with one argument renamed (and nothing else changed: the same values and kinds); the two get different answers, so each shows the station read the arguments (a PureSignal action with arguments it does not take is refused "The Core could not read this PureSignal request." before the transmit gate is asked); `verbs-ps3` (which requires `psAlgorithmVersion` 3) invokes the PureSignal verbs whose answers do not depend on arming (`ps3.off`, `ps3.twoTone` off and `ps3.saveCorrection`), and `verbs-ps3-arming` (which also requires `transmitSettingsVersion` 7) invokes `ps3.twoTone` with `enabled` true, a key (section 18.6) refused on that receive-only Core "This Core is set to receive only." (`refusalCode` `stationReceiveOnly`), and the arming verbs (`ps3.single`, `ps3.automatic`, `ps3.applyCurrent`, `ps3.restoreCorrection`), taken and then failing on the static station, which has no PureSignal running ("PureSignal is unavailable until the radio is ready."); `nnr.applyModelSelection` names the revision `dspAssets` gave in the snapshot; `verbs-tgxl-control` (which requires `remoteTgxlControlVersion` 2) invokes the antenna, operate and bypass switches, and `verbs-tgxl-relays` (which requires `remoteTgxlControlVersion` 4) matches `scanTgxlLan`'s `devicesJson` as any text, since a real Tuner Genius on the test computer's network may answer, and its accepted `setTgxlAddress` is followed by the `tuner` delta carrying the saved address, then a blank host, saved and shown as blank, and the address again; `verbs-pgxl-control` (which requires `remotePgxlControlVersion` 4) does the same for `scanPgxlLan` and `setPgxlAddress` (the `amplifier` delta, and the blank host), and its `setPgxlOperate` is refused on the static station, which has no amp connected ("The Core is not connected to the Power Genius."); `verbs-rfkit` (which requires `remoteRfKitControlVersion` 2) connects, disconnects and switches the RF-Kit, and `verbs-rfkit-control` (which requires `remoteRfKitControlVersion` 4) invokes `setRfKitOperate`, `setRfKitAntenna` and `setRfKitTciMode`, refused on the static station with no amp admitted ("The Core is not connected to the RF-Kit amplifier."), and its accepted `setRfKitAddress` is followed by the `rfkit` delta carrying the saved address, then the blank host and the address again; `verbs-tx-antenna` (which requires `radioHardwareVersion` 6) invokes `setAlexTxAntenna` with a band's current antenna (taken, no delta) and with `band` renamed; `verbs-io-board` (which requires `radioHardwareVersion` 7) invokes `requestIoBoardI2c` and `setIoBoardOutput`, each refused on the static station, which has no radio connection to reach the I2C bus through ("The radio is not connected, so its I2C bus cannot be reached."), and each with one argument renamed; `verbs-dsp-info` (which requires `dspInfoVersion` 1) invokes `dsp.filterResponse` with `highResolution` false (taken, `stepHz` 0 and an empty `magnitudesDbJson`), with `highResolution` true (refused on the static station, which runs no receiver channel: "The Core's receiver for this slice is not running.") and with one argument renamed. A version's new verbs go in a fixture of their own, so an app at the older version still runs the older file |

`tst_link_conformance_session` also checks that every verb in the
`commands` table is invoked both ways by some fixture, and that the two
legs of each get different answers.

The several-devices design's `sharing-budget` fixture (a holder and another
device over the Core's total: the holder's share is its whole request, the
other's reason `sharedConnection`, or `sharedProcessing` under the
governor's cut) needs the transmit holder, so it lands with Task 34; until
then `tst_display_budget_split` and `tst_station_multi_session` hold each
device's share, generation and reason (section 6.4).

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
`expect`. Where the encoder is exact (`nrsc1`, `dnssd-txt`, `ps3d`, `nsdc1`, `nsdx1`) it also
holds the encoder to the bytes: it encodes `expect` (or, for `nsdc1` and
`nsdx1`, the regen target's fixed inputs, and for the three malformed `nsdc1`
vectors and the refused `nsdx1` vectors those packets with the damage the
table names) again and
compares. Opus is not held
to its bytes, because its floating-point encoder may differ between
processors; its vectors hold decoders to the reference PCM instead.

| Codec | Vector | After | Decoded values |
| --- | --- | --- | --- |
| `nrsc1` | `lan-announcement`: one schema-1 LAN announcement datagram, as a Core from before schema 2 sends it (section 14.1) | none | `schema` 1, `controlPort`, `fingerprint`, `coreName`, `radioName`, `radioMac`, `radioConnected`, exact |
| `nrsc1` | `lan-announcement-2`: one schema-2 LAN announcement datagram, from a claimed Core whose pairing window was reopened (section 14.1) | none | `schema` 2, the fields above, `claimed` true, `identity` (base64url of the 32 bytes, no padding), `label` `KG4VCF/shack`, `pairing` `code`, exact |
| `nrsc1` | `lan-announcement-2-devices`: the `lan-announcement-2` datagram with the device count byte, 2, appended after Pairing (section 14.1) | none | The fields of `lan-announcement-2` and `devicesConnected` 2, exact |
| `nrsc1` | `lan-announcement-2-trailing`: the `lan-announcement-2-devices` datagram with five bytes appended after its known fields, which a reader ignores (section 14.1) | none | The same fields as `lan-announcement-2-devices`, and `ignoredTrailingBytes` 5: the vector's last five bytes are not decoded, and the encoder writes the bytes before them, exact |
| `dnssd-txt` | `dnssd-txt`: the Bonjour TXT record of the Core of `lan-announcement-2-devices` (section 14.2) | none | `serviceType` `_nereus-station._tcp` and `txt`, the entries as strings (`v`, `id`, `claimed`, `pair`, `name`, `devices`); the bytes are those entries in that order, exact |
| `ps3d` | `ps3d-frame`: one PureSignal display chunk, eight points and four correction points | none | Every header field and the eight value lists; `tolerance` `{"absolute": 0}`, because the values travel as IEEE-754 binary64 |
| `nsdc1` | `nsdc1-full`: frame 1, a keyframe | none | `disposition` `accepted`, `reason` `none`, `keyframe` (the header's keyframe flag), the context (`endpointId`, `contextGeneration`, `minDbm`, `maxDbm`), `encoderSequence`, `producerTimestamp`, `waterfallAdvance` and the reconstructed `traceDbm`, `waterfallDbm` and `wideDbm` rows; `tolerance` `{"dbm": 0.01}` |
| `nsdc1` | `nsdc1-delta`: frame 2, a delta | `nsdc1-full` | As above, `keyframe` false |
| `nsdc1` | `nsdc1-delta-after-loss`: frame 3, a delta, when frame 2 was lost | `nsdc1-full` | `disposition` `needKeyframe`, `reason` `sequenceGap`, no frame |
| `nsdc1` | `nsdc1-keyframe-after-loss`: frame 4, the keyframe the sender was asked for | `nsdc1-full` | `accepted`, `keyframe` true, the frame |
| `nsdc1` | `nsdc1-malformed-delta`: frame 2's delta with its trace plane's block size code set to 4 (no such size), to a fresh decoder | none | `disposition` `rejected`, `reason` `malformed`, `keyframe` false, no frame (not `needKeyframe` `noHistory`) |
| `nsdc1` | `nsdc1-malformed-stale-delta`: frame 2's delta with sequence 0 (older than frame 1's) and its last byte cut off | `nsdc1-full` | `rejected`, `malformed`, `keyframe` false, no frame (not `staleSequence`) |
| `nsdc1` | `nsdc1-malformed-keyframe`: frame 4's keyframe with its trace plane claiming one block more than its length needs, to a decoder that needs a keyframe | `nsdc1-full`, `nsdc1-delta-after-loss` | `rejected`, `malformed`, `keyframe` true, no frame |
| `nsdx1` | `nsdx1-full`: the display extras datagram beside `nsdc1-full`'s endpoint, every section (three blobs, a 32-sample peak hold row, the noise floor, the waterfall's levels) | none | `accepted` true, `reason` `none`, `endpointId`, `contextGeneration`, `encoderSequence`, `peakBlobs` (`pixel`, `dbm`), `peakHoldDbm` (the decoder's dequantised row), `noiseFloorDbm`, `waterfallLevelsDbm` (`lowDbm`, `highDbm`); `context` names the endpoint context it decodes against; `tolerance` `{"dbm": 0.01}` |
| `nsdx1` | `nsdx1-noise-floor`: the noise floor section alone | none | As above, with `noiseFloorDbm` the only section |
| `nsdx1` | `nsdx1-other-generation`: `nsdx1-full`'s bytes, against generation 2 | none | `accepted` false, `reason` `contextMismatch` |
| `nsdx1` | `nsdx1-unknown-section`: `nsdx1-full` with section bit `0x10` set | none | `accepted` false, `reason` `unknownSections` |
| `nsdx1` | `nsdx1-truncated`: `nsdx1-full` with its last byte cut off | none | `accepted` false, `reason` `truncated` |
| `opus` | `opus-1` to `opus-4`: four consecutive RTP packets of the speakers' mix at the station's settings (48 kHz stereo, 1920 samples per packet, 24000 bit/s, wideband, the Core's default `audio_bitrate`). A receiver stream runs Opus at 48000 bit/s fullband whatever `audio_bitrate` says (media control document, receiver audio) and is read by the same decoder; its `encoder` object reports the rate | the packets before it | `status` `accepted`, `sequence`, `timestamp`, `channels` 2, `bandwidth` 1103 (Opus wideband), `samplesPerChannel` 1920, and `pcm16`, the station decoder's output as 16-bit values (`round(sample * 32767)`); `ssrc` is the packets' RTP source, which the decoder is given; `tolerance` `{"minSnrDb": 60}` |

The three `nsdc1-malformed-*` vectors hold a decoder to the display codec
document's order ("State and recovery"): the whole datagram's structure
first (the codec bound, the header, and every plane), then the endpoint,
context, generation, sequence and history rules, then the datagram is
applied. A datagram that is both malformed and refused is `rejected` with
its structure reason, whatever state the decoder is in, and leaves history
untouched.

An NSDC `dbm` tolerance applies to every number of the decoded frame; an
NSDX one to every number of the decoded datagram, whose expectation lists
exactly the sections it carries (display extras v1, section 4). An
Opus tolerance is either `{"minSnrDb": N}` (the decoded PCM is at least N
dB above its difference from `pcm16`) or `{"lsb16": N}` (no sample is more
than N 16-bit steps from `pcm16`); the vector states which.

### 16.5 Running the station's runners

```
cmake --build build --target tst_link_conformance_control tst_link_conformance_session tst_link_conformance_media
QT_QPA_PLATFORM=offscreen ctest --test-dir build -R '^tst_link_conformance_(control|session|session_connectable|media)$' --output-on-failure
```

The session runner is registered twice: `tst_link_conformance_session`
runs every fixture but those whose `radio` is `"connectable"`, and
`tst_link_conformance_session_connectable` runs those alone
(`NEREUS_LINK_CONNECTABLE` set to `skip` and `only`; unset, the binary
runs every fixture).

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
  `property.result`, `settings.reject`, `pair.fail`, `confirm.request`
  and `notice`, a take choice's `why`, the three strings of `change`, and
  the display's
  `rejected` and `allocation-result`) passes `OperatorWording::isPlain` and names no
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
  `stationTci` object's `error`, the `txState` object's `stopText`
  (section 18.8), the `amplifier` object's
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
  windows in use compare as they are. Device names and short names (the
  `devices` and `connectedDevices` objects, section 7.1) are the
  operator's own words, checked as names (section 3.5) and never held to
  `isPlain`, whose terms would refuse an ordinary name such as "Grant's
  iPhone"; a sentence of the station's that carries one is checked with
  the name set aside.

## 18. Transmit

iPhone app plan Task 34 (R-IOS-02, R-IOS-03, R-IOS-13; the several-devices
design, rulings 7.4, 8.1 to 8.5, 8.8, 8.13 and 8.15). This section is the
Core's side of remote transmit that exists today. Keying from a device
(`tx.key`, `tx.unkey`, `tx.tune`, `tx.twoTone`, section 18.6) came with
Task 35, and the microphone line with keying on a filled buffer (section
18.6) with Task 36; the watchdog, its keepalive and the microphone
starvation action (section 18.7) with Task 37; the transmit state
(`txState`, section 18.8) with Task 39; taking transmit (`tx.take`) with
Task 77.

### 18.1 Who may transmit

A session may transmit (`txPermitted`, section 6.3) when all of these hold,
in this order; the first that fails is its refusal (18.3):

1. the Core's `remote_transmit` is allow (`nereusd.conf`, default allow; a
   desktop that hosts a Core is deny) (`stationReceiveOnly`);
2. its hello declared `remoteTx` 1 at minor 11 (`notReady`, "Update this
   app");
3. it signed in with a paired device's key, not the older token
   (`notReady`, "Pair this device");
4. `snapshot.complete` has been sent to it (`notReady`);
5. transmit is unheld, or its device holds it (`otherDeviceHolds`,
   `changingHands`, `stopNotConfirmed`).

With `remote_transmit` deny the Core is receive-only, as every Core was
before: it refuses every key, its own radio's PTT included.

### 18.2 The holder of transmit

One device at a time holds transmit, keyed or not; it starts unheld. The
radio's own PTT (its microphone or a footswitch) makes the Core's own
position the holder, named "Radio", on unheld transmit and, taking it
without a question, from another holder (section 18.9). A person's key on
unheld transmit makes its device the holder; a program's key never does.
While another device holds transmit, every other device's key is refused;
a device takes transmit with `tx.take` (section 18.9). A holder keeps
transmit after its key ends, the Core's own position included, until
another takes it or it is released.

Every change of holder, and every release, is a transfer: every key from
every source is refused ("Transmit is changing hands."); a keyed holder is
unkeyed through the unkey-confirmed gate (receive reached, or 2000 ms and
the transmitter stopped at once); if the radio still reads keyed the Core
stops it again and waits up to 2000 ms more, and if it still reads keyed
transmit ends unheld and every key is refused ("The radio did not confirm
it stopped transmitting.") until it reads off. A new holder always starts
unkeyed, and VOX is turned off at every change of holder.

A holder whose link drops is unkeyed at once, keys refused until the radio
reads off, and keeps transmit, away and unkeyed, for its 3 minutes; the
same device signing in again within them still holds it, unkeyed, and keys
with its next press. At the end of its 3 minutes, on `session.leave` and
on its revocation, transmit becomes unheld through a transfer.

`connectedDevices` (section 7.1) shows it: `holdsTransmit` is true for the
holder, keyed or not; `state` is `transmitting` while it is on the air, and
`transmittingForSeconds` how long.

### 18.3 Refusals

A refused key, `tx.setTxSlice` or on-air change carries one of these. A
`command.result` refused this way has the sentence as its `reason` and, in
its `values`, `refusalCode` (the code) and `refusalFix` (the fix, or empty)
as `utf8` entries; a `property.result` and `settings.reject` carry the
sentence. A client shows the sentence as sent and may offer the fix.
"<holder>" is the holder's name as `connectedDevices` numbers it, or
"Radio"; "<short name>" is its short name.

| Code | Sentence | Fix |
| --- | --- | --- |
| `notReady` | This device is still connecting to the Core. Try again in a moment. | |
| `notReady` | Update this app to transmit through this Core. | |
| `notReady` | Pair this device with the Core to transmit through it. | |
| `stationReceiveOnly` | This Core is set to receive only. | |
| `bandPlan` | The band plan's own sentence (for example "Frequency outside TX-allowed range"), or "The band plan does not allow transmitting here." | |
| `interlock` | The radio's transmit inhibit input is holding transmit off. | |
| `interlock` | The transmit interlock is holding transmit off. Check it in Setup. | |
| `ampStandby` | The amplifier is in standby. Operate it, or change the interlock in Setup. | `operateAmp` |
| `paProtection` | The amplifier has tripped. Reset it before transmitting. | |
| `swr` | The SWR is over the interlock's limit. Check the antenna, or change the interlock in Setup. | |
| `otherDeviceHolds` | <holder> has the transmitter. | `takeTransmit` |
| `programNeedsTransmit` | A program can transmit only while this device has transmit. Take transmit here first. | `takeTransmit` |
| `micNotReady` | Microphone is not ready. Check Audio settings and retry. (the Core's own microphone); This device's microphone is not connected to the Core yet. Wait a moment and try again. (a remote voice key with no microphone line, section 18.6); No sound has reached the Core from this device's microphone yet. Wait a moment and try again. (a remote key whose line carried no sound within 250 ms, section 18.6) | |
| `changingHands` | Transmit is changing hands. Try again in a moment. | |
| `stopNotConfirmed` | The radio did not confirm it stopped transmitting. | |
| `holderOnAir` | <short name> is on the air. Try again when they stop. ("The radio is on the air. Try again when it stops." while the radio's own PTT, or the Core's own keys, hold transmit) | `takeTransmit` |
| `notHolder` | Take transmit on this device first. | `takeTransmit` |
| `otherDeviceHolds` | <holder> has the transmitter. Take it to stop the transmission. (`tx.unkey`, or TUNE or two-tone off, from a device that does not hold transmit) | `takeTransmit` |
| `keyEnded` | The Core already stopped this transmission. Key again to transmit. | |

`changingHands` and `stopNotConfirmed` are the several-devices design's two
sentences without codes; `keyEnded` answers a copy of a key the Core has
already stopped (section 18.6); `holderOnAir` is ruling 7.4's; `notHolder` answers
`tx.setTxSlice` while nobody holds transmit, a case the design does not
settle.

### 18.4 While the holder is on the air

While the holder is keyed, these changes from any other device are refused
with `holderOnAir`, never asked; the holder's own change is not refused by
this rule, and with the holder unkeyed none is. Every holder counts, the
station device's own keys included (the radio's PTT, and the Core's own MOX,
TUNE or a Tuner Genius hardware TUNE): the rule names changes, not holders.
The holder changes its transmit antennas, and the Alex tab's three transmit
high-pass switches (section 8.1), on the air as in Thetis; another device's
change waits. The saved amplifier and tuner addresses
(`setTgxlAddress`, `setPgxlAddress`, `setRfKitAddress`) and the LAN scans go
ahead on the air; the addresses are still asked of a device that holds
transmit (the several-devices design, table 7.1). The station device has no
session, so a change that would disturb only it applies without a question.
The words name the holder by its short name; "The radio is on the air. Try
again when it stops." after the radio's own PTT took transmit, and for the
Core's own keys on a Core no desktop hosts; a hosting desktop's own key is
named after the desktop.

- the transmit path: the amplifier (`configurePgxl`, `disconnectPgxl`,
  `setPgxlConnectionSettings`, `setPgxlName`, `setPgxlHardware`,
  `setPgxlNetwork`, `savePgxlSettings`), the tuner (`configureTgxl`,
  `disconnectTgxl`, `setTgxlName`, `setTgxlNetwork`, `saveTgxlSettings`,
  `setTgxlAntenna`, `setTgxlOperate`, `setTgxlBypass`, and the `tuner`
  object's operate, bypass and antenna and the `amplifier` object's
  operate), an antenna (`setAlexRxAntenna`, a slice's `rxAntenna` or
  `txAntenna`, the `alexAntennas` object), PureSignal (the
  `pureSignalSettings` object, `transmit`'s `pureSig`, and every `ps3.*`
  verb but `ps3.twoTone` and `ps3.subscribeDisplay`), the interlock
  (`setTxInterlockPolicy`) and the power cap (`setPgxlPowerCap`);
- a sample-rate change on a Protocol 1 radio (`requestSliceSampleRate`),
  which stops the radio's data flow;
- a C-Tune centre change on the receiver of the holder's transmit slice
  (`requestStreamCentre`).

While the station device is keyed (the radio's own PTT, its mic or
footswitch, or the Core's own keys), the slice it transmits on is frozen for
every device, its owner's included (the several-devices design, ruling 8.11;
D64): a write of its `frequency`, `dspMode`, `filterLow`, `filterHigh`,
`txAntenna`, `band`, `xitEnabled` or `xitHz`, `removeSlice` or
`slice.selectBand` for it, a pan move that would carry, move or close it (the
owner's `requestStreamCentre` to a free receiver, or the anchor's move of its
receiver), and a sample-rate change that reaches it, are refused
`holderOnAir` with the holder's on-air words. A change asked before the key
and confirmed during it is refused at `confirm.proceed` the same way, and
nothing of it applies; so is a take of the transmit slice or its receiver.
The freeze ends with the key, and the transmit flag never moves while it
holds (`tx.setTxSlice`, a change of holder).

### 18.5 `tx.setTxSlice`

`tx.setTxSlice {sliceId}` moves the transmit flag to the slice with that
id (never a list position). It is the holder's: from another device while
transmit is held it is refused `otherDeviceHolds`, and while nobody holds
transmit `notHolder`; for a slice that is not the holder's own it is
refused with the words every slice verb uses for another device's slice
(iPhone app plan Task 77, ruling 8.10). The holder's choice is remembered:
when it takes transmit again, the flag goes to that slice if it is still
its own, otherwise to its active slice. When the holder's transmit slice
closes, the flag moves to another of its slices; with none left, transmit
is released through a transfer to nobody (ruling 8.12). While the holder is keyed the transmitter is unkeyed
through the unkey-confirmed gate first and the flag moves once receive is
reached; unkeyed it moves at once. The result is `accepted` when the move
is asked; the slices' `txSlice` deltas carry the move.

### 18.6 Keying

iPhone app plan Task 35 (R-IOS-13; spec section 4.6 item 7; D51, D58,
D63; the several-devices design, section 2.2 and rulings 8.3, 8.5 and
8.14). Four verbs, under `remoteTxVersion` 1 at minor 11, for a peer that
declared `remoteTx` 1 (any other peer is refused "Update this app to
transmit through this Core."):

- `tx.key {trigger}`: key the transmitter on the transmit slice.
  `trigger` is `"screen"`, `"headset"`, `"bluetooth"`, `"actionButton"`
  (a person's key) or `"tci"` (a program's key, sent by a remote window's
  TCI server for an app); any other value is refused "The Core could not
  read this request."
- `tx.unkey {epoch}`: release the key with that epoch. The transmitter
  stops now (the normal unkey; TUNE and two-tone end their own way).
- `tx.tune {on}`: TUNE on or off; on keys at the tune power.
- `tx.twoTone {on}`: the two-tone test on or off.
- `tx.tunerTune {on}` (`remoteTxVersion` 2, iPhone app plan Task 77): the
  Core's Tuner Genius autotune, as the Core's own Tuner page runs it (the
  amplifier to standby, the tune carrier, the tuner's sweep, the amplifier
  back to operate). It is a key for every rule below, answered with the
  epoch its carrier takes; the carrier starts once the amplifier is in
  standby (up to 1.5 s later), keyed as this device, so the watchdog
  watches it (section 18.7), and ends when the tuner finishes, or after
  3 s when the tuner never starts its sweep. `{on:false}`, or `tx.tune
  {on:false}`, ends this device's cycle, keyed or still waiting. The
  session's and the holder's refusals come first; then, while the radio is
  on the air (MOX on or walking back to receive, or TUNE; the device's own
  key included), it is refused `holderOnAir` "The radio is on the air. Try
  again when it stops." with no fix, since the amplifier is never switched
  between standby and operate while RF flows; then in plain words "No
  Tuner Genius is connected to the Core." when none is, and "The tuner is
  already tuning." while any cycle runs. While this device's cycle waits
  for the amplifier, its own `tx.key`, `tx.tune {on:true}`, `tx.twoTone
  {on:true}` and `ps3.twoTone {enabled:true}` are refused "The tuner is
  already tuning."; if the radio goes on the air meanwhile (a VOX key), the
  cycle ends without keying and the amplifier returns to operate once the
  radio is back on receive. A take (section 18.9) ends the old holder's
  cycle before the transfer.

Every key passes the Core's gates in the order of section 18.1, then TX
inhibit, the PA trip, receive only, the band plan and the interlock, and
only then the holder's rule (section 18.2), as every key at the Core does;
a refused one is refused with its code (section 18.3), and a key refused
before the holder's rule takes nothing. TUNE and two-tone meet TX inhibit,
the PA trip, receive only and the interlock before the holder's rule, and
the band plan in the mode they transmit in (TUNE swaps CW for sideband
first); a TUNE or two-tone refused after the holder's rule never keys, and
the take it made is released, so a refused TUNE or two-tone, a device's or
the station's (its TUNE button, a Tuner Genius hardware TUNE), takes
nothing either.

**Who may key.** A person's key (`tx.key` with any trigger but `"tci"`,
and `tx.tune`, `tx.twoTone`, `tx.tunerTune` and `ps3.twoTone` on) while
transmit is unheld makes that device the holder, unkeyed, and then keys;
nobody is asked, and the transmit flag goes to that device's own slice
(ruling 8.10). The holder
keys. Another device's key while transmit is held, keyed or not, present
or away, is refused `otherDeviceHolds`. A program's key
(`tx.key {trigger:"tci"}`) never takes transmit: it keys only while its
device already holds transmit, and is otherwise refused
`programNeedsTransmit` (unheld) or `otherDeviceHolds` (held by another
device), and nobody becomes the holder. While a device holds transmit, a
VOX key at the Core is that device's.

**Who may release.** `tx.unkey`, `tx.tune` and `tx.twoTone` off, and
`ps3.twoTone {enabled:false}`, are the holder's: from a device that does not hold transmit they are refused
`otherDeviceHolds` ("<holder> has the transmitter. Take it to stop the
transmission.") and the transmission continues. A release ends only that
device's own key (a VOX key while it holds transmit is its own). The Core's
own safety stops end whoever is keyed.

**The keying epoch.** An accepted `tx.key`, `tx.tune {on:true}` or
`tx.twoTone {on:true}` answers with the key's epoch in its `values`:
`epoch` (`i64`, 1 to 4294967295, advancing with every key; a refused key
spends none). `tx.unkey` names it. A device sends each key and each unkey
three times, as the same command (the same verb and `id`); the Core acts
on the first and answers every copy:

- a copy of an accepted key is answered with the same epoch while that
  key is on, and refused `keyEnded` once it has ended (released, stopped
  by the Core, or taken), so a delayed copy never keys again;
- a copy of a refused key is answered with the same refusal;
- a `tx.key` with a new `id` while the device's own key is on changes
  nothing and is answered with that key's epoch;
- a `tx.unkey` whose epoch is older than the device's key now on is
  ignored (answered accepted; nothing changes), and one with nothing of
  the device's on is answered accepted.

A session's commands are forgotten when it ends. A device whose link
drops is unkeyed at once (section 18.2) and, when it signs in again, nothing
keys until it sends a new `tx.key`: no replay, no resume.

An accepted `tx.unkey`, and an accepted TUNE or two-tone off, carry no
values.

**Keying on a filled buffer (Task 36).** A device whose media connection
carries the microphone line (its media `start` carried `remoteTxVersion`;
remote media control document, Microphone line) sends its microphone while
it transmits. Its `tx.key`, in a mode that transmits the microphone (every
mode but CWL and CWU), is answered once the line's buffer holds its 60 ms
target, and then keys; when the buffer has not filled within 250 ms the key
is refused `micNotReady`, "No sound has reached the Core from this device's
microphone yet. Wait a moment and try again." The holder's own refusals come first, at once.
Copies of a waiting key, and a new `tx.key` from the same device, get the
waiting key's answer; a `tx.unkey` from the device while its key waits
answers that key `keyEnded`, and it never keys. `tx.tune` and
`tx.twoTone` use no microphone and key at once. A voice key (`tx.key` with
any trigger, a program's included, in a mode that transmits the microphone)
from a device whose media carries no microphone line (no media yet, as
right after a reconnect, or a media start without `remoteTxVersion`) is
refused at once `micNotReady`, "This device's microphone is not connected
to the Core yet. Wait a moment and try again.", after the session's own
refusals and the holder's, and nothing keys: the Core never puts its own
microphone on the air for a remote key. While a device is keyed on its
line, or has VOX armed, the transmitter takes that line instead of the
Core's configured microphone; at unkey the configured source returns with
the line's buffer empty. Several devices may carry a line at once (each
media connection its own); one line feeds the transmitter at a time: the
keyed device's, else the line of the device whose key is waiting for its
buffer, else the VOX device's (the holder when it has VOX armed). Every
other line is received and dropped, never mixed in, and a change of line
starts from silence. One device's media ending never closes another's
line, and a keyed device whose media restarts stays on silence until its
new line carries audio. A VOX key while the device has VOX armed is that
device's, as a VOX key while it holds transmit already is.

**Programs through a remote window.** A remote window's TCI server
forwards an app's `trx:N,true` as `tx.key {trigger:"tci"}` and its
`trx:N,false` as `tx.unkey` for the window's own key. It takes the TCI
transmit audio for the app only after the Core accepts the key; a refused
key takes nothing, and the app hears `trx:N,false` while the window shows
the Core's sentence. The Core's own TCI server stays receive-only: a
program through it never keys, held or unheld.

**The desktop's remote window.** It keys as a phone does: its MOX (the TX
applet's button and the container's) sends `tx.key {trigger:"screen"}`, its
TUNE `tx.tune`, its two-tone `tx.twoTone`, each command three times under
one `id`; its own MoxController keys nothing. A release names the key's
epoch, or 4294967295 when released before the answer came (never older
than the device's live key, so a key accepted just before the release
still stops). After the Core ends a key on its own (its `transmitting`
falls), the next press is a new command. Its TUNE off goes whenever it
asked TUNE on, even before the Core's `tune` reached it. A program's
release while the operator's own MOX holds the same key on sends nothing;
the operator's MOX off ends a program's key too. The window's VOX button
writes `transmit.voxEnabled` (a permitted session's write, section 18.1),
and while it is on the window streams its microphone on the microphone
line unkeyed.

### 18.7 The watchdog and microphone starvation

iPhone app plan Task 37 (R-IOS-13; remote design sections 8.3, 12.1 and
12.3; pairing design section 9.7; spec section 4.6 items 1 and 2).

**The numbers, stated together.** A device keyed, or with VOX armed, sends
a keepalive every 100 ms. The Core stops transmitting once more than
400 ms pass without one from it (the link-loss deadline). A keyed device's
microphone line counts as starved after 250 ms without audio (the
starvation deadline). The Core's transmit buffer for the line targets
60 ms and never holds more than 120 ms. A client's first reconnect comes
1000 ms after a loss. So 120 < 250 < 400 < 1000: starvation is handled
before the link counts as lost, and the Core has stopped before any
reconnect (`RemoteTxWatchdog`, `RemoteMicConfig`, checked at compile time).

**`tx.keepalive {sequence, epoch}`.** Under `remoteTxVersion` 1 at minor
11, for a peer that declared `remoteTx` 1 (any other is refused "Update
this app to transmit through this Core."), both arguments `i64`:

- `sequence`: 1 for the first keepalive of a session, then one more with
  each keepalive the device sends, whichever path it takes; it never starts
  again while the session lasts. 0, or more than 9007199254740991, is
  refused "The Core could not read this request."
- `epoch`: the epoch of the device's key now on, from its answer (section
  18.6); 4294967295 while a key's answer has not come and for a key the
  device was never answered for (a VOX key, TUNE and two-tone when it did
  not keep theirs), which is never older. 0 to 4294967295.

It is sent once, never three times: a lost one is overtaken by the next.
It is answered `accepted` with no values, whether or not it counted, and
refused only when unreadable.

**What the Core watches.** A device while `keyedBy` names it (its own key,
its program's, TUNE, two-tone, or a VOX key that is its), and a device
whose accepted write turned `transmit.voxEnabled` on, while VOX stays on.
Watching starts with a fresh 400 ms. A keepalive counts when its
`sequence` is newer than the last that counted from that device (a copy
by another path, or one overtaken, does not) and its `epoch` is not older
than the device's key's. The device's own release (`tx.unkey`, TUNE or
two-tone off) ends the watch on its key at once, so a transmission that
ends by itself after a release (a RADE end-of-over tail, later) is never
taken for a lost link.

**When it stops.** More than 400 ms without a keepalive that counts, or
the device's session ending (a drop, leaving, a replacement or a
revocation), and the Core turns off the VOX that device armed and, when
the key on the air is that device's, stops transmitting at once (the
emergency stop, then StopAllTx) with "The link to <device> went quiet, so
the Core stopped transmitting.", "<device>" being its name as
`connectedDevices` gives it. Nothing keys again by itself: the device's
next key is a new `tx.key` (section 18.6).

**The paths.** On the session's WebSocket the keepalive is this verb. On a
media connection with the microphone line it travels on that connection's
`tx` data channel instead (unordered, never retransmitted; 13 bytes, remote
media control, "The "tx" data channel"), so a lost keepalive never waits
behind a retransmission: at 5 % loss the channel's keepalives never trip
the watchdog in ten minutes, while the same loss on a reliable in-order
channel does (`tst_remote_tx_watchdog`). The rendezvous, relay and separate
control connection (iPhone app plan Tasks 26 to 29) hand their keepalives
to the same rules.

**VOX a device armed.** A device whose media carries no microphone line
cannot arm it: its `transmit.voxEnabled` write is refused "This device's
microphone is not connected to the Core yet. Wait a moment and try again."
That refusal is the backstop: a client shows its VOX control disabled with
the same words while its media carries no microphone line (the desktop's
TX applet VOX button and Setup's Enable VOX do). It goes off when that
device's session ends, when its link goes quiet and
when its microphone line closes. While it is on
and that device's line does not carry the audio VOX listens to, a VOX key
at the Core is refused (`micNotReady`): the Core never keys from its own
microphone because a device armed VOX.

**Microphone starvation on a live link.** When the device's line starves
while it is keyed on it, the transmit mode decides: LSB, USB, DSB, CWL,
CWU, DIGL, DIGU and SPEC stay keyed (silence there puts no carrier on the
air; WDSP's DSB adds none), until the time-out or the operator ends it;
AM, SAM, FM, DRM, RADE_U and RADE_L stop at once with "No microphone audio
arrived from <device>, so the Core stopped transmitting." TUNE and
two-tone use no microphone and are never stopped by it.

### 18.8 The transmit state (`txState`)

iPhone app plan Task 39 (D14, R-IOS-13, R-IOS-21; spec section 5.5 items 5
and 8). The `txState` object (`TransmitState`, `txStateVersion` 1; 2 adds
the holder of transmit and `keyedForSeconds`, appended after `stopSerial`,
and `stopEpoch` after them) goes to
a peer at minor 11 whose hello declared `remoteTx` 1, in its snapshot after
`connectedDevices`, and as deltas. Every property is `outbound`; a write
is refused as any outbound property's is.

| Property | Meaning |
| --- | --- |
| `keyed` | The radio is on the air (MOX, TUNE or two-tone): the radio object's `transmitting` |
| `tuning`, `twoTone` | TUNE is on; the two-tone test is on |
| `txSliceId` | The slice transmit is bound to, -1 for none |
| `keyedByName`, `keyedByKind`, `keyedTrigger` | Who keyed (the device's name as `connectedDevices` numbers it, its kind) and how (the `tx.key` trigger, `tune`, `twoTone`, `vox`, or the Core's own `radioPtt`, `cat` or `station`); `""` while unkeyed |
| `keyedSinceMs` | When the key began, in milliseconds on the Core's own monotonic clock (the one `connectedDevices`' durations use); 0 while unkeyed. Compare it only with another `keyedSinceMs` |
| `timeOutRemainingSeconds` | Whole seconds before the transmit time-out stops this key, -1 when no time-out applies (unkeyed, or the limit for this device's kind is off). Phones and tablets have their own limit, 180 s by default |
| `forwardPowerWatts`, `reflectedPowerWatts`, `swr` | The radio's power readings, as the Core's own power and SWR meters show them; `swr` is 1.0 with no forward power |
| `alcDb`, `micLevelDb` | The ALC and MIC readings as the Core's meters show them (Thetis's readings: ALC floored at -30 dB, MIC at -195 dB); -400, no reading, while the Core has no transmit channel |
| `txEnding` | True only during a RADE end-of-over tail; no tail is built yet, so always false |
| `stopReason` | Why the Core last stopped a transmission on its own: `""` (none since the Core started), `linkLost`, `micStarved`, `timeOut`, `takenOver`, `revoked` or `station` |
| `stopText` | That stop in plain words, for an app to show as sent |
| `stopSerial` | Advances by one with each such stop (serial-number arithmetic, as `devices`' `revision`); 0 before the first |
| `stopEpoch` | The keying epoch (the `epoch` `tx.key`, `tx.tune` or `tx.twoTone` answered with) of the key that stop ended; 0 before the first stop. An app ends only a key of its own whose epoch is this one or older: a key it pressed after the stop, answered before the stop's update arrived, goes on |
| `holderDeviceId` | Who holds transmit (the several-devices design, ruling 8.1; `txStateVersion` 2): the device's id as `connectedDevices` sends it, `station` for the Core's own position (the radio's PTT, the Core's own keys); `""` while unheld |
| `holderName`, `holderShortName`, `holderKind` | The holder's name and short name as `connectedDevices` numbers them, and its kind; for the station device, kind `station`, named after the desktop that hosts the Core for that desktop's own MOX or TUNE (ruling 8.1), and "Radio", "Radio" after the radio's own PTT took transmit or on a Core no desktop hosts; `""` while unheld |
| `holderSource` | How it got transmit: `device`, or `radioPtt` after a take by the radio's own PTT (a mic or a footswitch); read this, never the name, to tell the radio from a device; `""` while unheld |
| `holderForSeconds` | How long it has held transmit, in whole seconds on the Core's clock when this is sent (ruling 10.3; the app counts on from its receipt); 0 while unheld |
| `holderEpoch` | Advances with every change of holder (a take, a release); the same while the holder keys, unkeys or goes away |
| `holderAway` | The holder's link dropped and it keeps transmit, unkeyed, for its 3 minutes |
| `holderTransferring` | Every key is refused while it is true: transmit is changing hands, or a dropped holder's key is being stopped ("Transmit is changing hands."), or, with no holder, the radio did not confirm it stopped transmitting after a transfer ("The radio did not confirm it stopped transmitting.") until MOX reads off |
| `keyedForSeconds` | How long the key now on has been on, in whole seconds on the Core's clock when this is sent (ruling 10.3); 0 while unkeyed. It supersedes `keyedSinceMs`, which a Core still sends |

**When it is sent.** While keyed the Core reads the meters ten times a
second, from the transmit lane's last readings (never a DSP call on its
event loop), and sends a reading that changed, with the time left when it
changed. While unkeyed only changes are sent: the power readings as the
radio reports them (they fall to 0 at the unkey), nothing else. Changes
travel in the 50 ms delta flush like any other.

**Stops.** Each key (each rise of `keyed`) is stopped at most once: the
first reason the Core records for it advances `stopSerial` and sets
`stopReason` and `stopText` together, and a later one for the same key
changes nothing. A device's own `tx.unkey`, and TUNE or two-tone off, are
no stop. The reasons:

- `timeOut`: the transmit time-out (iPhone app plan Task 38): "Transmit
  stopped after 3:00, the Core's time-out for phones and tablets." for a
  phone or tablet, "Transmit stopped after 3:00, the Core's transmit
  time-out." for any other, and "Transmit stopped: the Core's network
  check went unanswered for 3:00." for the ping time-out, each with its
  limit;
- `linkLost`: the device on the air went quiet (the watchdog, section
  18.7), or its session ended, or the holder connected again on another
  link, while it was on the air: "The link to <device> went quiet, so the
  Core stopped transmitting.", the one sentence the Core's log and toast
  say too;
- `revoked`: the holder was removed from the Core while on the air:
  "<device> was removed from the Core, so the Core stopped transmitting.";
- `micStarved`: no microphone audio arrived from the device on the air
  in a mode that stops on it (the starvation action, section 18.7): "No
  microphone audio arrived from <device>, so the Core stopped
  transmitting.";
- `takenOver`: another device or the radio took transmit while the holder
  was on the air (iPhone app plan Task 77): "<taker> took transmit, so the
  Core stopped transmitting." ("Radio took transmit, ..." for the radio's
  own PTT);
- `station`: any other stop the Core made itself: "The Core stopped
  transmitting."

The Core sends the object to the holder and to every other declaring peer,
so a device that lost its link learns why from the snapshot when it signs
in again. A device that was removed is not connected to learn it.

### 18.9 Taking transmit

iPhone app plan Task 77 (R-IOS-02, R-IOS-03; the several-devices design,
sections 8.4 to 8.7, rulings 8.1, 8.6 to 8.12 and 7.7; `remoteTxVersion`
2).

**`tx.take {holderEpoch, shownKeyed}`** (both optional, sent together or
not at all; for a peer with `sessionHolderVersion` 1, any other is refused
"Update this app to transmit through this Core."). It never keys: the
device keys with its next `tx.key`, which it may send straight after an
accepted take. The session's own gate (section 18.1, items 1 to 4) comes
first. Then:

- transmit unheld: taken at once, answered accepted with `values`
  `holderEpoch` (the epoch after the take);
- the device already holds it: accepted, `holderEpoch`, nothing changes;
- another holder, and `holderEpoch` equals `txState.holderEpoch` and the
  holder is not on the air unless `shownKeyed` is true (the device asked
  its operator first, ruling 8.7): taken at once;
- another holder otherwise: refused "Waiting for you to confirm." with
  `phase` `needsConfirmation`, then a `confirm.request` of kind
  `takeTransmit` (section 7.5), `affected` empty, with `holder`, the
  holder's entry: `{deviceId, name, shortName, kind, source, state, keyed,
  connectedForSeconds, lastActivitySeconds, awayForSeconds,
  transmittingForSeconds}`, `source` `device` or `radioPtt` (then "Radio",
  kind `station`, `deviceId` `station`), `state` `transmitting` while the
  holder is on the air (the red "Unkey and take over"), `away` in its
  3 minutes (asked without red; nothing to unkey), else `listening`;
- a transfer running, or the radio not confirming its stop: refused
  `changingHands` or `stopNotConfirmed`.

A copy of a `tx.take` (the same command id, which a device repeats as it
does every transmit command) is never asked again and never takes twice:
while the first one's take runs, that take's one result answers the id;
after it, a copy gets the same answer again.

`confirm.proceed {id, choice: -1}` takes transmit; when the holder was not
on the air when asked and is now, or the holder changed, it is answered
"Waiting for you to confirm." and a new `confirm.request` follows (the
operator always sees the red question before a carrier is cut); when
transmit became unheld meanwhile it takes at once.

**The take** is the transfer of section 18.2: a keyed holder is unkeyed
through the unkey-confirmed gate before anyone is assigned, every key is
refused meanwhile ("Transmit is changing hands."), and the taker holds
transmit unkeyed. The answer (of `tx.take` or of the proceed) arrives when
the transfer ends: accepted with `holderEpoch`, or refused
`stopNotConfirmed`. A holder taken from on the air has its stop recorded
(`stopReason` `takenOver`). It is told with a `notice` of kind
`transmitTaken`, "<taker> took transmit.", naming the taker (`bySource`
`radioPtt` and "Radio" for the radio's own PTT), with Take it back. A
holder taken from while away does not get transmit back when it returns:
its notice waits and follows its `snapshot.complete`. A taker whose own
session ends while its take runs holds transmit away when the take ends
(its 3 minutes running) if it dropped, and transmit is unheld if it left
or was a token window.

**Take it back.** `notice.takeBack {id}` of a `transmitTaken` notice is
`tx.take {}` with its usual question (red when the taker is on the air,
the radio's microphone included) and answers as `tx.take` does. A notice
is taken back at most once.

**The radio's own PTT** (its mic or a footswitch, the radio's PTT bit),
at a press edge: with the Core's own position already holding transmit it
keys, the names and source unchanged; otherwise it takes transmit without
a question, unkeying a holder on the air first, whatever its key
(`tx.key`, TUNE, two-tone, `tx.tunerTune` or VOX), and keys only once the
transfer ends and only while still pressed. A press that takes nothing (TX
inhibit, the PA trip, receive only, a transfer already running) is held
off until it is released, so it never keys later without a fresh press. A press released during the
transfer keys nothing. A press still held after a device takes transmit
back does not take it again; the next press does, and releasing it never
unkeys a device's key. `txState` then names "Radio" with `holderSource`
`radioPtt`. A desktop that hosts the Core does not take on its MOX or
TUNE while another device holds transmit: that key is refused
`otherDeviceHolds`, and the desktop takes by these rules first.

**The transmit slice.** A take moves the transmit flag to the new
holder's own slice (its last choice, else its active slice); the radio's
own PTT transmits where the flag is. `txSlice` marks a slice only while its
owner holds transmit (section 7.1).

**The transmitter's own settings** (ruling 7.7). While transmit is held,
a write to the `transmit` object from another device is refused
"<holder> has the transmitter.", and so is `txProfile.select` (with
`refusalCode` `otherDeviceHolds`); with transmit unheld any device may
change them. Turning `transmit.voxEnabled` on needs holding transmit: from
another device it is refused "<holder> has the transmitter.", and with
transmit unheld "Take transmit on this device first.". A carrier action
(`tx.tune`, `tx.twoTone`, `tx.tunerTune`, `ps3.twoTone` on) is a key
(section 18.6).

## 19. The rendezvous

A client reaches a Core it cannot address directly through the rendezvous
at `rv.nereussdr.com` (or a self-hosted one), specified in its own document,
[2026-09-23-rendezvous-v1.md](2026-09-23-rendezvous-v1.md), with its
conformance suite in `rendezvous/conformance/v1/`. It uses this document's
identity values unchanged (section 3.4): the Core registers under an id
derived from its station identity key, a device introduces itself by its
device id (section 3.5) with a signature by its device key, and pairing by
code carries section 3.6's `pair.*` messages, as text, inside the
rendezvous's mailbox messages. The rendezvous introduces the two ends and
mints relay credentials; the session that follows is this link, on its own
connection, direct or through the relay, never through the rendezvous.

**Pairing through a mailbox** (iPhone app plan Task 27). A mailbox carries
the `pair.*` messages of section 3.6 and nothing else: no `hello` goes
either way and no `session.end`. The device sends `pair.start` in code
mode as the mailbox's first message (one tap never pairs through it: a
mailbox has no address of its own), and the exchange then runs exactly as
on a direct connection, the station's `pair.fail` or `pair.confirm` ending
it. A message of any other kind from the device ends the pairing as a
protocol error. A mailbox has no certificate, so the device keeps the
Core's identity key from the station's box and checks the certificate
binding against the certificate of its first sign-in (section 3.4). The
Core gives its nameplate back only after that mailbox has closed, since
releasing a nameplate ends its mailbox.
