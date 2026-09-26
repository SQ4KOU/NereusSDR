# Rendezvous version 1

This is the written specification of the rendezvous: the small signalling
service at `rv.nereussdr.com` (and at any self-hosted address) that lets a
client reach a Core it cannot address directly, and carries the pairing
exchange when the two are not on one network. It is part of R-IOS-08 (pair
by code through the relay) and R-IOS-16 (direct and relay) of the
[iPhone app plan](2026-09-23-iphone-app-plan.md), Task 26, and D38 of the
[iPhone app design](2026-09-23-iphone-app-design.md). The design behind it is
the [station identity and pairing design](2026-08-02-remote-station-identity-and-pairing-design.md),
sections 5.1 to 5.5.

Three parties use it:

- **The service**, `rendezvous/server/` in this repository.
- **A station**: the Core (`nereusd`) in its station role, which registers
  under its id and answers introductions (iPhone app plan Task 27).
- **A client**: the desktop's remote window (Task 27) or the iPhone app
  (Task 27a), which introduces itself to a station, or opens a pairing
  mailbox.

The conformance suite in `rendezvous/conformance/v1/` holds all three to
this document (section 10). Where the service's code and this document
disagree, this document is the authority and the code is the bug.

Section numbers are stable. Later revisions add sections; they never
renumber.

## 1. What the rendezvous does and does not do

The rendezvous introduces; it never carries a session. It does four things:

1. **Registration.** A station proves it holds its identity key and is then
   reachable under its id (section 6.2).
2. **Introductions.** A client passes an offer to a station by id, the
   station answers, and both trickle ICE candidates through the service
   (section 6.3). The session itself then runs on its own ICE connection,
   direct or through the relay, never through the rendezvous.
3. **Relay credentials.** When a station accepts an introduction and allows
   the relay, the service mints short-lived TURN credentials for coturn and
   gives the same ones to both ends (section 8).
4. **Pairing mailboxes.** A station claims a nameplate (the number in its
   pairing code) and a client opens the mailbox on it; the two exchange the
   link's `pair.*` messages as opaque text (section 6.5).

It never learns a label, a callsign, a pairing code's words or a device's
name, and it cannot tell whether a device is paired: it matches opaque ids,
forwards what it is given untouched, and keeps everything in memory
(section 9). What it can see is in the pairing design, section 5.2: the
addresses of both ends, that an id is online, and traffic timing.

## 2. Transport

- One WebSocket per connection, over TLS on TCP 443: `wss://rv.nereussdr.com/`
  (a self-hosted server has its own host name). The path is `/`; the service
  ignores it. On the NereusSDR server the website's Caddy terminates TLS and
  forwards the WebSocket to the service on loopback by host name; the service
  itself never listens on a public address.
- Messages travel as WebSocket text frames, each one JSON object, encoded
  compactly, with a string `type` naming its kind. A binary frame is a
  protocol error (section 7).
- The service caps each inbound message at 131072 bytes (128 KiB) at the
  WebSocket layer, before anything is decoded; a longer one closes the
  connection with close code 1009 and no error message. The field caps of
  section 5.2 are smaller and are checked when a message is decoded.
- The service sends a WebSocket ping every 20 s and closes a connection whose
  pong has not come back 20 s later (section 9.2). Pings are control frames,
  not messages. Clients answer pings as any WebSocket stack does and need
  not send their own.
- The service closes a connection with close code 1000 after an error that
  closes (section 7), and with 1001 when it is shutting down. The reason a
  peer acts on is always in the `error` message before the close, never in
  the close frame, whose reason text is empty.

## 3. Roles and the life of a connection

Every connection starts the same way: the service sends `hello` (section
6.1) at once. The connection's first message decides its role, for the rest
of its life:

| First message | Role |
| --- | --- |
| `register` | station |
| `introduce` or `mailbox.open` | client |
| anything else | a protocol error: `error` `protocolError`, then the close |

A station connection serves one id. A client connection holds at most one
live introduction and at most one open mailbox at a time; when one ends it
may start another on the same connection. A message of the other role's
kinds is a protocol error.

Timers (defaults in section 9.1, all configurable):

- **Handshake.** A connection that has not sent its first message within
  10 s, or a station that has not finished registering within 10 s of its
  connection opening, gets `error` `timeout` and the close.
- **Idle.** A client connection with no live introduction and no open
  mailbox for 30 s gets `error` `idle` and the close. The timer starts when
  the client's first message has been answered without leaving anything
  pending (an `offline` answer, say) and whenever its introduction or mailbox
  ends. A registered station is never idle; the pings keep its connection
  and its NAT mapping alive.

## 4. Identity values

### 4.1 Keys and signatures

The values are the station link's, section 3.4
([2026-09-23-station-link-v1.md](2026-09-23-station-link-v1.md)), with no
change:

- A **public key** is strict base64url (no padding, only `A-Z a-z 0-9 - _`,
  the unused low bits of the last character zero, a length that is not 1 mod
  4) of a canonical 91-byte P-256 SubjectPublicKeyInfo DER with its point
  uncompressed: 122 characters. Canonical means it loads as a P-256 key and
  encodes back to the same 91 bytes. Nothing else is accepted.
- A **signature** is ECDSA P-256 over SHA-256, raw `r || s`, 64 bytes,
  strict base64url: 86 characters. Not DER.
- A **device id** is the link's device id (section 3.5): the SHA-256 of the
  device's SPKI DER, 32 bytes, strict base64url: 43 characters. It is the
  device's `id` in the Core's paired devices, not its key.

### 4.2 The station's rendezvous id

A station registers under, and a client introduces itself to, the station's
**rendezvous id**:

```
id = base32(SHA-256("NereusSDR rendezvous id v1\n" || SPKI DER))[0..26]
```

- `SPKI DER` is the 91 bytes of the Core's **station identity key**, the
  P-256 key of link section 3.4: the key the Core sends as `identity`
  `publicKey` in its `hello` and in `pair.accept`. It is never derived from
  the TLS certificate's key, which is a different key and may change.
- The prefix is the 27 ASCII bytes `NereusSDR rendezvous id v1` followed by
  one line feed (0x0A).
- `base32` is RFC 4648 base32 with the standard alphabet, lowercased, with
  no padding. The id is its first 26 characters (130 of the digest's 256
  bits), so it matches `[a-z2-7]{26}`.

The id is fixed for the life of the key (the pairing design, section 5.2:
rotation is deferred). A client that paired with a Core holds its key from
`pair.accept` and derives the id itself. This section is the only
definition of the id; the pairing design points here.

`rendezvous/conformance/v1/crypto/rendezvous-id.json` gives keys, their
digests and their ids.

### 4.3 The registration proof

After `register`, the service sends a `challenge` whose `nonce` is 32 bytes
from the operating system's random generator, new for every registration.
The station signs, with its station identity key:

```
"NereusSDR rendezvous register v1\n" || nonce
```

where `nonce` is the **32 raw bytes** the challenge's base64url decodes to,
not its text. The prefix is 33 ASCII bytes ending in one line feed. The
transcript is 65 bytes. `crypto/register-proof.json` gives a test-only key,
a nonce, the transcript bytes and valid and invalid signatures.

### 4.4 The introduction signature

Every connection's `hello` carries its own `nonce`, 32 random bytes, new per
connection. A client introducing itself on that connection signs, with its
device key:

```
"NereusSDR introduce v1\n" || station id || nonce
```

where `station id` is the 26 ASCII bytes of the station's rendezvous id
(section 4.2) and `nonce` is the **32 raw bytes** the connection's `hello`
`nonce` decodes to, not its text. The prefix is 23 ASCII bytes ending in one
line feed; the transcript is 81 bytes.

The service does not verify this signature: it cannot know which devices a
station has paired. It forwards `device` and `deviceSignature` untouched and
adds the `nonce` of the introducing connection from its own record, never
from anything the client sent. The station verifies the signature with the
key of the paired device whose id is `device`, and answers nothing when the
device is not paired, revoked, or the signature does not verify (iPhone app
plan Task 27). Binding the nonce stops an observer who saw one introduction
from replaying it on a connection of its own; binding the station id stops a
signature for one Core being shown to another.
`crypto/introduce-signature.json` gives a test-only device key, a station
id, a nonce, the transcript bytes and valid and invalid signatures,
including one over the nonce's text.

## 5. Messages

### 5.1 The envelope

Each message is a JSON object with a string `type`. Keys are case
sensitive. The rules for every message the service receives:

- Every key its kind lists must be present with the kind and length
  section 5.2 gives, or the message is a protocol error.
- A key its kind does not list is ignored, and never forwarded: the service
  builds each forwarded message from the listed keys alone.
- A kind the service does not know, a kind of the other role, JSON that
  does not parse, a top level that is not an object, a missing or non-string
  `type`, a key that appears twice in one object, `NaN` or `Infinity`, and a
  string holding a lone surrogate (an escape such as `\ud800` that is not a
  Unicode scalar value) are protocol errors.
- A whole number is a JSON number written without a fraction or an
  exponent. `true` and `false` are not numbers, and `7.0` and `"7"` are not
  whole numbers.

A string's length is counted in bytes of its UTF-8 encoding. "Forwarded
unchanged" means the forwarded string is the same sequence of Unicode scalar
values, so the same UTF-8 bytes; the escapes a sender chose (`é` for
`é`) are not preserved, because JSON text is re-encoded, and nothing a
receiver decodes differs.

The service encodes compactly, keys in the order the tables below list
them, and writes non-ASCII characters as UTF-8 rather than escapes.
Receivers must not depend on key order.

### 5.2 Field kinds

| Kind | Meaning |
| --- | --- |
| `rid` | a rendezvous id: exactly 26 characters of `a-z` and `2-7` (section 4.2) |
| `key` | a public key: strict base64url of exactly 91 bytes, 122 characters (section 4.1); whether it is a canonical P-256 key is checked after decoding |
| `sig` | a signature: strict base64url of exactly 64 bytes, 86 characters |
| `device` | a device id: strict base64url of exactly 32 bytes, 43 characters |
| `nonce` | strict base64url of exactly 32 bytes, 43 characters |
| `intro` | an introduction id: strict base64url of exactly 16 bytes, 22 characters, chosen by the service at random; opaque, never an address |
| `sdp` | a string of 1 to 65536 bytes (UTF-8): an SDP offer or answer, passed on untouched |
| `candidate` | a string of 0 to 4096 bytes: one ICE candidate line, passed on untouched; the empty string is the end of candidates |
| `body` | a string of 1 to 65536 bytes: one mailbox message, passed on untouched |
| `nameplate` | a whole number from 1 to 999999 |
| `bool` | `true` or `false` |
| `version` | a whole number from 1 to 65535 |
| `code` | 1 to 64 ASCII letters (`A-Z a-z`); a receiver treats a code it does not know as the generic form of its message |
| `reason` | a string of 1 to 1024 bytes: plain words an app may show as sent |
| `retry` | a whole number from 0 to 2147483647: milliseconds to wait before trying again; 0 means no advice |
| `urls` | an array of 0 to 8 strings, each 1 to 512 bytes: STUN or TURN URLs (RFC 7064, RFC 7065) |
| `turn` | `null`, or an object with exactly these keys: `username` (a string of 1 to 512 bytes), `password` (a string of 1 to 128 bytes), `expires` (a whole number from 0 to 4294967295, Unix seconds) and `urls` (`urls`); section 8 |

### 5.3 The kinds

**From a station to the service:**

| Kind | Keys | When |
| --- | --- | --- |
| `register` | `id` (`rid`), `publicKey` (`key`) | first message; section 6.2 |
| `prove` | `signature` (`sig`) | after `challenge` |
| `answer` | `to` (`intro`), `answer` (`sdp`), `turn` (`bool`) | once per introduction it accepts |
| `candidate` | `to` (`intro`), `candidate` (`candidate`) | after its `answer` to that introduction |
| `nameplate.claim` | none | registered |
| `nameplate.release` | none | registered |
| `mailbox` | `body` (`body`) | while a mailbox is open on its nameplate |
| `mailbox.close` | none | while a mailbox is open on its nameplate |

**From a client to the service:**

| Kind | Keys | When |
| --- | --- | --- |
| `introduce` | `id` (`rid`), `device` (`device`), `deviceSignature` (`sig`), `offer` (`sdp`) | no live introduction on this connection |
| `candidate` | `candidate` (`candidate`) | while its introduction is live |
| `mailbox.open` | `nameplate` (`nameplate`) | no open mailbox on this connection |
| `mailbox` | `body` (`body`) | while its mailbox is open |
| `mailbox.close` | none | while its mailbox is open |

**From the service to a station:**

| Kind | Keys | Sent |
| --- | --- | --- |
| `hello` | `version` (`version`), `nonce` (`nonce`), `stun` (`urls`) | first, on every connection |
| `challenge` | `nonce` (`nonce`) | after `register` |
| `registered` | `id` (`rid`) | after a valid `prove` |
| `introduction` | `from` (`intro`), `device` (`device`), `deviceSignature` (`sig`), `offer` (`sdp`), `nonce` (`nonce`) | a client introduced itself to this id |
| `credentials` | `from` (`intro`), `turn` (`turn`) | after the station's `answer` with `turn` true |
| `candidate` | `from` (`intro`), `candidate` (`candidate`) | the client sent one |
| `introduction.end` | `from` (`intro`), `code` (`code`): `clientLeft` or `expired` | the introduction ended |
| `nameplate` | `nameplate` (`nameplate`) | after `nameplate.claim` |
| `nameplate.released` | none | after `nameplate.release` |
| `mailbox.opened` | `nameplate` (`nameplate`) | a client opened the mailbox on its nameplate |
| `mailbox` | `body` (`body`) | the client sent one |
| `mailbox.closed` | `code` (`code`): `closed`, `peerClosed`, `peerLeft` or `expired` | the mailbox closed |
| `error` | `code` (`code`), `reason` (`reason`), `retryAfterMs` (`retry`) | section 7 |

**From the service to a client:**

| Kind | Keys | Sent |
| --- | --- | --- |
| `hello` | `version` (`version`), `nonce` (`nonce`), `stun` (`urls`) | first, on every connection |
| `answer` | `answer` (`sdp`), `turn` (`turn`) | the station answered |
| `candidate` | `candidate` (`candidate`) | the station sent one |
| `introduction.end` | `code` (`code`): `stationLeft` or `expired` | the introduction ended |
| `mailbox.opened` | `nameplate` (`nameplate`) | after `mailbox.open` |
| `mailbox` | `body` (`body`) | the station sent one |
| `mailbox.closed` | `code` (`code`): `closed`, `peerClosed`, `peerLeft`, `released` or `expired` | the mailbox closed |
| `error` | `code` (`code`), `reason` (`reason`), `retryAfterMs` (`retry`) | section 7 |

A station or client logs and ignores a message it cannot decode or a kind it
does not know; the service ends the connection on one (section 5.1). That
is the station link's rule (link section 13) applied here.

## 6. Flows

### 6.1 The greeting

```
service -> any:  {"type":"hello","version":1,"nonce":<nonce>,"stun":[<url>, ...]}
```

`version` is the rendezvous version the service speaks, 1 here. `nonce` is
32 random bytes, new for this connection, which an introduction on this
connection signs (section 4.4). `stun` lists the STUN servers from the
service's configuration, plain STUN needing no credentials; on
`rv.nereussdr.com` one IPv6-only and one IPv4-only name (section 8). A
client uses what it needs of the list (the pinned libjuice uses one STUN
server).

### 6.2 Registration

```
station -> service:  {"type":"register","id":<rid>,"publicKey":<key>}
service -> station:  {"type":"challenge","nonce":<nonce>}
station -> service:  {"type":"prove","signature":<sig>}
service -> station:  {"type":"registered","id":<rid>}
```

- Before it sends a challenge, the service checks that `publicKey` is a
  canonical P-256 key and that `id` is the id derived from it (section 4.2).
  Either failing is `error` `proofFailed` and the close. Without the id
  check, anyone could register anyone's id.
- `prove` must verify over the registration transcript with this
  connection's challenge (section 4.3), or it is `error` `proofFailed` and
  the close. Only a verified `prove` registers the id.
- A second registration of the same id, on another connection, replaces the
  first: the older connection gets `error` `replaced` and the close, and its
  introductions, nameplate and mailbox end as when a station leaves
  (sections 6.3 and 6.5). A Core that restarts or moves networks therefore
  takes its id back at once.
- A `prove` before `register`, a second `register` on one connection, and
  any other station kind before `registered` are protocol errors.

### 6.3 Introductions

```
client  -> service:  {"type":"introduce","id":<rid>,"device":<device>,"deviceSignature":<sig>,"offer":<sdp>}
service -> station:  {"type":"introduction","from":<intro>,"device":...,"deviceSignature":...,"offer":...,"nonce":<the client connection's hello nonce>}
station -> service:  {"type":"answer","to":<intro>,"answer":<sdp>,"turn":true|false}
service -> client:   {"type":"answer","answer":<sdp>,"turn":<turn object>|null}
service -> station:  {"type":"credentials","from":<intro>,"turn":<the same turn object>|null}   (only when turn was true)
either  -> service -> other:  candidates, then the end of candidates
```

- **Only its own id.** An introduction goes to the connection registered
  under `id`, and to no other. When no connection is registered under `id`,
  whether the id was never seen, its station left, or its station has not
  finished registering, the client gets `error` `offline` (section 6.4).
- **The introduction id.** `from` (and the station's `to`) is 16 random
  bytes the service picks for this introduction, unique among live ones. It
  names the introduction; it is never an address.
- **Accepting.** The station answers only an introduction it accepts: the
  device is paired and the signature verifies (section 4.4). A station that
  does not accept stays silent; nothing tells the client, which waits for
  its own deadline or the introduction's lifetime. The station answers at
  most once per introduction; a second `answer`, or one naming an
  introduction that has ended, gets `error` `unknownIntroduction` and the
  connection stays.
- **Relay.** `turn` true in the station's `answer` asks for relay
  credentials: the station allows the relay (`relay = allow` in
  `nereusd.conf`, iPhone app plan Task 27). The service mints them then, and
  only then (section 8), and gives the same object to both ends: in the
  client's `answer` and in a `credentials` message to the station. When the
  service has no relay configured it sends `turn` null in both, so a station
  that asked always gets its `credentials` reply. With `turn` false the
  client's `turn` is null, nothing is minted and no `credentials` is sent.
- **Candidates.** Each end trickles its ICE candidates through the service,
  one `candidate` message per candidate line, forwarded untouched. The
  client may send candidates as soon as its introduction is live; the
  station only after its `answer` (a station candidate before it is a
  protocol error). An empty `candidate` is the end of candidates and is
  forwarded like any other. At most 64 non-empty candidates per side per
  introduction are forwarded; the next gets `error` `tooManyCandidates` and
  is dropped, and the connection stays. The end of candidates does not
  count. A client candidate with no live introduction gets `error`
  `noIntroduction` (it can race the introduction's end), and a station
  candidate for an introduction that has ended gets `error`
  `unknownIntroduction`; the connection stays in both cases.
- **Ending.** An introduction ends when either end's connection closes (the
  other gets `introduction.end` `clientLeft` or `stationLeft`), when the
  station's registration is replaced (`stationLeft`), or when its lifetime
  runs out: 120 s after `introduce` by default, covering candidate
  gathering (up to 23.5 s) and ICE's 39.5 s connectivity timer with room to
  spare, when both ends get `introduction.end` `expired`. A client that has
  its session running simply closes its rendezvous connection. A second
  `introduce` while one is live is a protocol error.
- **Order of the client's connection.** A client gathers with the relay
  credentials it receives in `answer`, so an offer carries no candidates of
  its own; both ends' candidates arrive by trickle. The station's
  credentials arrive in `credentials` just after its answer goes out.

### 6.4 Unknown and offline

`introduce` to an id with no registered station answers exactly these bytes,
whatever the reason:

```
{"type":"error","code":"offline","reason":"The Core is not reachable right now. Check that it is running and connected to the internet.","retryAfterMs":0}
```

An id that was never registered, one whose station left, and one whose
station is between `register` and `prove` are indistinguishable by content.
They are distinguishable from an online station that stays silent, by
timing: `offline` comes back at once, a silent station's refusal never
does. That is acceptable because it reveals nothing the rendezvous does not
already show: that an id is online is exactly what it must know to
introduce anyone, and the pairing design (section 5.2) lists it among what
the rendezvous sees. What it does not reveal is whether a given device is
paired, because the station's silence looks the same for an unpaired device,
a revoked one and a bad signature.

The rate limits count introductions to an id the same way whether or not
it is online (section 9.1), so they tell nothing either.

### 6.5 Nameplates and mailboxes

```
station -> service:  {"type":"nameplate.claim"}
service -> station:  {"type":"nameplate","nameplate":<n>}
client  -> service:  {"type":"mailbox.open","nameplate":<n>}
service -> client:   {"type":"mailbox.opened","nameplate":<n>}
service -> station:  {"type":"mailbox.opened","nameplate":<n>}
either  -> service -> other:  {"type":"mailbox","body":<the pair.* message's JSON text>}
one end -> service:  {"type":"mailbox.close"}
service -> it:       {"type":"mailbox.closed","code":"closed"}
service -> other:    {"type":"mailbox.closed","code":"peerClosed"}
```

- **Nameplates** are numbers from 1 to 999999, global to the service: the
  number in a pairing code (link section 3.6). A registered station claims
  one and gets the lowest number no station holds. A station holds at most
  one: a second `nameplate.claim` answers the number it already holds. It
  keeps it until it sends `nameplate.release` (answered with
  `nameplate.released`, also when it held none) or its connection ends.
  When all 999999 are held, a claim gets `error` `nameplatesExhausted` and
  the connection stays.
- **A mailbox** is the one conversation on a nameplate. `mailbox.open` on a
  nameplate no station holds gets `error` `nameplateUnknown`; on one already
  serving a mailbox, `error` `nameplateBusy`; in both cases the connection
  stays. On success both ends get `mailbox.opened`. A nameplate never serves
  two mailboxes at once. A second `mailbox.open` while this connection's
  mailbox is open is a protocol error.
- **Bodies.** `body` is the text of one link `pair.*` message (link section
  3.6), JSON as text, forwarded unchanged (section 5.1). The service does
  not read it. At most 32 bodies per side per mailbox are forwarded; the
  next gets `error` `tooManyMessages` and is dropped. A body with no open
  mailbox gets `error` `noMailbox`; both leave the connection open.
- **Closing.** A mailbox closes when either end sends `mailbox.close` (it
  gets `mailbox.closed` `closed`, the other `peerClosed`), when either end's
  connection ends (the other gets `peerLeft`), when the station releases
  its nameplate (the client gets `released`; the station gets only
  `nameplate.released`), or 300 s after it opened (both get `expired`). A
  `mailbox.close` with no open mailbox gets `error` `noMailbox`.
- **After a mailbox.** A closed mailbox frees the nameplate for the next
  one; the nameplate itself stays with the station until it releases it or
  leaves. So one pairing burns one code (link section 3.6), and the
  station's next code can keep its number.

The rendezvous learns nothing it could test guesses against (link section
3.6): the SPAKE2 exchange runs inside the bodies. Anyone can open a
mailbox on a guessed small number, which uses up the station's current
code; the per-address limit on `mailbox.open` (section 9.1) and the link's
attempt ceiling bound that.

## 7. Errors

`{"type":"error","code":<code>,"reason":<reason>,"retryAfterMs":<retry>}`
is the only error message. `code` is what a peer acts on; `reason` is the
exact text below, which an app may show as sent; `retryAfterMs` is the
value below, or for `rateLimited` the milliseconds until the attempt would
be allowed (at least 1). An error that closes is followed at once by the
close (code 1000, or 1001 for `shuttingDown`); the others leave the
connection open.

| Code | Closes | `reason` | `retryAfterMs` | Cause |
| --- | --- | --- | --- | --- |
| `protocolError` | yes | "A message could not be read, so the connection was closed. Updating the app or the Core may help." | 0 | section 5.1; a message out of turn or of the other role; a binary frame |
| `proofFailed` | yes | "The Core could not prove its identity, so it was not registered." | 0 | a key that is not a canonical P-256 key, an id not derived from the key, a `prove` that does not verify (section 6.2) |
| `replaced` | yes | "The Core registered again on another connection, so this one was closed." | 0 | the same id registered on another connection |
| `timeout` | yes | "The connection did not finish starting in time." | 0 | the handshake timer (section 3) |
| `idle` | yes | "The connection was closed because it was not being used." | 0 | the idle timer (section 3) |
| `tooManyConnections` | yes | "Too many connections are open from this network. Try again shortly." | 5000 | the per-address cap, sent instead of `hello` |
| `overloaded` | yes | "The connection server is busy. Try again shortly." | 5000 | the total cap, sent instead of `hello` |
| `shuttingDown` | yes | "The connection server is restarting. Try again shortly." | 5000 | the service is stopping |
| `offline` | no | "The Core is not reachable right now. Check that it is running and connected to the internet." | 0 | section 6.4 |
| `rateLimited` | no | "Too many attempts. Try again in a minute." | until allowed | section 9.1 |
| `noIntroduction` | no | "There is no connection attempt in progress." | 0 | a client candidate with no live introduction |
| `unknownIntroduction` | no | "That connection attempt has already ended." | 0 | a station's `answer` or `candidate` naming no live introduction of its own, or a second `answer` |
| `tooManyCandidates` | no | "Too many network addresses were offered for one connection attempt." | 0 | section 6.3 |
| `nameplateUnknown` | no | "No Core is showing that pairing code right now. Check the code and try again." | 0 | section 6.5 |
| `nameplateBusy` | no | "Another device is pairing with this Core right now. Try again shortly." | 5000 | section 6.5 |
| `nameplatesExhausted` | no | "Pairing codes are not available right now. Try again shortly." | 5000 | section 6.5 |
| `noMailbox` | no | "There is no pairing in progress on this connection." | 0 | section 6.5 |
| `tooManyMessages` | no | "Too many pairing messages were sent." | 0 | section 6.5 |

A peer treats a code it does not know as an error that may or may not
close; it reads the close when it comes. `offline`'s bytes never change
(section 6.4).

## 8. Relay credentials

The relay is coturn with its time-limited credentials (`use-auth-secret`,
`static-auth-secret`). The service and coturn share one secret; the service
mints:

```
expires  = now (Unix seconds) + 86400
username = "<expires>:<station id>"            e.g. "1800086400:m3xq..."
password = base64(HMAC-SHA1(secret, username)) standard base64, with padding
```

- `secret` is the bytes of coturn's `static-auth-secret` as written in its
  configuration (UTF-8); the service reads the same bytes from its secret
  file, without the trailing line end.
- The credentials are valid for 24 hours and are minted only when a station
  accepts an introduction with `turn` true (section 6.3). The same object
  goes to both ends, so the client and the station allocate with the same
  username. The username carries the station id so coturn's quotas and
  logs group by station, never by device.
- `urls` in the object lists the configured TURN servers. On
  `rv.nereussdr.com` that is an IPv6-only and an IPv4-only relay name, each
  on UDP 3478 and UDP 443 (the pinned libjuice resolves one address per TURN
  host, preferring IPv4, so each family needs a name of its own; the
  pairing design section 9.5 item 3 requires both families). The defaults
  are `rv6.nereussdr.com` and `rv4.nereussdr.com`:
  `turn:rv6.nereussdr.com:3478?transport=udp`,
  `turn:rv6.nereussdr.com:443?transport=udp`,
  `turn:rv4.nereussdr.com:3478?transport=udp` and
  `turn:rv4.nereussdr.com:443?transport=udp`, and the `stun` list of
  `hello` names the same two hosts on 3478. The names are configuration.
- What coturn does when an allocation is refreshed after its credential has
  expired is recorded by the coturn check in Docker (Task 26's second part,
  `rendezvous/tests/coturn-check.sh`), and decides whether clients refresh
  credentials sooner (Task 29).

`crypto/turn-credentials.json` gives secrets, expiries, ids, usernames and
passwords, the passwords computed with `openssl dgst -sha1 -hmac`, not with
the service's code.

## 9. Limits, storage and logs

### 9.1 Limits

Every value is configurable (`rendezvous.conf`, `[limits]`); these are the
defaults.

| Limit | Default | Why |
| --- | --- | --- |
| Introductions per source address | 30 a minute | the brief's figure; a person retrying by hand never meets it |
| Introductions per station id | 60 a minute | counted for every id asked for, online or not (section 6.4) |
| Mailbox opens per source address | 10 a minute | each open can use up a pairing code; guessing nameplates is slow |
| Candidates per side per introduction | 64 | more than any real gathering, within the message caps |
| Bodies per side per mailbox | 32 | a pairing exchange is under ten each way |
| Connections per source address | 16 | a household with several phones and Cores behind one address |
| Connections in total | 4096 | bounds memory; `overloaded` beyond it |
| Handshake timeout | 10 s | a registration is one round trip plus a signature |
| Idle timeout for clients | 30 s | a client with nothing pending has no reason to stay |
| Introduction lifetime | 120 s | gathering (23.5 s) plus ICE's 39.5 s timer, with room |
| Mailbox lifetime | 300 s | the pairing exchange, with Argon2id hashing on the Core, and a person typing |
| WebSocket ping interval and timeout | 20 s and 20 s | keeps a station's NAT mapping and Caddy's upstream alive; the station link uses 20 s too (link section 12.1) |
| Outbound queue per connection | 256 messages | a peer that stops reading is closed rather than buffered without bound |

The windows are sliding: a limit of N a minute refuses an attempt when N
were counted in the last 60 s, with `retryAfterMs` until the oldest leaves
the window. A refused attempt is not counted. A source address counts by
itself for IPv4 (and an IPv4-mapped IPv6 address as its IPv4 address) and by
its /64 for any other IPv6 address, as the station link counts handshakes
(link section 12.3).

The source address is the peer's, or, when the peer is a configured trusted
proxy (by default the loopback addresses, where Caddy connects from), the
last address in `X-Forwarded-For`, which is the one the proxy itself
appended. The service listens on loopback only, IPv4 and IPv6
(`127.0.0.1` and `::1`, port 8710 by default), so every peer is the proxy.

### 9.2 Timers and pings

The handshake and idle timers are section 3. The ping is the WebSocket
layer's: the service pings every 20 s and closes a connection whose pong is
20 s late. A station behind NAT needs traffic to keep its mapping; 20 s is
under every home router's TCP timeout and matches the station link's
heartbeat.

### 9.3 Storage

The service keeps everything in memory. It writes nothing to disk: no
station, id, key, address, introduction, nameplate or mailbox, no cache and
no log file. A restart forgets every registration, and stations register
again on their next connection (a station reconnects with its usual
backoff). The only files it reads are its configuration and its TURN secret
at start. `rendezvous/tests/test_nothing_on_disk.py` runs the service as its
own process with an empty working directory, `HOME` and `TMPDIR`, drives
every kind of exchange through it, and checks all three are still empty.

### 9.4 Logs

Logs go to standard error only; under systemd that is the journal. A log
line may name an event (registered, replaced, left, an introduction and how
it ended, a nameplate claimed or released, a mailbox opened or closed, an
error code) and the first six characters of a station id or introduction id.
It never holds a whole id, an address, a label, the TURN secret, a minted
username or password, an SDP, a candidate, a mailbox body, a nonce, a
signature, a public key or a nameplate number (the number is part of a
pairing code). The websockets library's own log records, which can carry
addresses, are switched off.

## 10. Conformance

`rendezvous/conformance/v1/` is the machine-readable half of this document.
Three runners read it: the service's (`rendezvous/tests/`), the Core's
station role (iPhone app plan Task 27) and the clients' (the desktop,
Task 27; the iPhone app, Task 27a). It follows the station link's
conformance format (link section 16) wherever that fits; the differences
come from there being three parties rather than two, and are named below.

### 10.1 The files

- `manifest.json`: `{"rendezvousVersions":[1],"fixtures":[{"id","file","kind"}]}`,
  `kind` one of `crypto`, `control` and `session`. Every file under
  `crypto/`, `control/` and `sessions/` is listed once. There is no
  `requires`: the rendezvous has no capabilities.
- `crypto/*.json`: the derivation and signature vectors (section 10.2).
- `control/*.json`: one message each (section 10.3).
- `sessions/*.json`: one scripted exchange each (section 10.4).
- `sdp/offer.sdp`, `sdp/answer.sdp`: the SDP texts a runner sends where a
  fixture says `"$sdp:offer:<name>"` or `"$sdp:answer:<name>"`.

The two test-only private keys in `crypto/` are marked as such in the file
(`testOnly`). They were made for the vectors, protect nothing, and must
never be used as an identity; every other key in the suite is made at run
time.

### 10.2 Crypto vectors

| File | Holds | A runner checks |
| --- | --- | --- |
| `base64url.json` | `cases`: `{"text","valid","bytesHex"}` or `{"text","valid":false,"why"}` | its strict base64url decoder accepts exactly the valid texts, to those bytes |
| `p256-spki.json` | `cases`: `{"name","spkiHex","valid"}`: a canonical key, a compressed point, P-384, secp256k1, a point off the curve, a BIT STRING with unused bits, a trailing byte, a truncated key | its key check accepts exactly the valid one |
| `rendezvous-id.json` | `prefixHex` and `cases`: `{"publicKey","digestHex","id"}` | SHA-256 of the prefix and the key's DER is `digestHex`, and the id derived from it is `id` (section 4.2) |
| `register-proof.json` | `key` (`testOnly`, `privateKeyPkcs8` as base64url of PKCS#8 DER, `publicKey`, `id`), `nonce`, `transcriptHex`, and `cases`: `{"name","publicKey","signature","valid"}` | the transcript of `nonce` is `transcriptHex`, and a case is valid exactly when its key is a canonical P-256 key and its signature (strict base64url, 64 bytes, raw) verifies over it: another nonce, a flipped bit, another key, a DER signature, padding, a short signature and three bad keys are not |
| `introduce-signature.json` | `device` (`testOnly`, `privateKeyPkcs8`, `publicKey`, `id`), `stationId`, `nonce`, `transcriptHex`, and `cases`: `{"name","signature","valid"}` | the transcript of `stationId` and `nonce` is `transcriptHex`, `id` is the fingerprint of `publicKey`, and a case verifies with the device's key exactly when it is valid: another nonce, another station, the nonce's text, a flipped bit and another device are not |
| `turn-credentials.json` | `cases`: `{"secret","expires","stationId","username","password"}` | the username and password from section 8 |

### 10.3 Control fixtures

`{"from":"station"|"client"|"server","to":"station"|"client","wire":{<the message>},"decodes":true|false}`.
`to` is present only when `from` is `"server"`, since a server message's
shape depends on which role receives it; a message from a station or a
client always goes to the service. This is the one difference from link
section 16.2's `{"from","wire","decodes"}`.

The receiving end decodes `wire`; when `decodes` is true it encodes the
result again and the two compare equal as JSON values, key order ignored.
The service's runner decodes every fixture, in every direction, with the
service's decoder for that direction, and also sends each refusal from a
station or client to the running service, which must answer `error`
`protocolError` and close. A station's runner decodes the fixtures `to`
`"station"` and encodes those `from` `"station"`; a client's runner does the
same for `"client"`.

The fixtures cover every kind in each direction it travels (section 5.3):
eight from a station, five from a client, thirteen to a station and eight to
a client, with both ends of candidates, relay and no relay, and codes a
receiver does not know. The refusals: an `id` in capitals and one of 25
characters; a padded key, a key that is a number, a `register` without
`publicKey`; a 63-byte signature; `turn` as a string; an empty `answer`; an
`answer`, an `offer` and a `body` over 65536 bytes (one counted in
two-byte characters); a candidate over 4096 bytes and a null candidate; a
`to` of 15 bytes; a station `candidate` without `to`; a `body` that is an
object; a device key where a device id belongs; a padded device signature;
an `introduce` without `offer`; nameplates 0, 1000000, `"7"`, `7.5` and
`true`; unknown kinds from each role and each role's kinds from the other;
a `type` that is a number; and, for a client or station decoder, a `turn`
that is a string or lacks `password`, a `hello` `version` that is a string,
an `introduction` without `nonce`, a negative `retryAfterMs`, a
`credentials` sent to a client and an unknown kind.

The 128 KiB transport cap is enforced before decoding, so no fixture holds
it; `test_control.py` checks it against the running service.

### 10.4 Session fixtures

`{"runs":[<runners>],"serverSetup":{...},"pairedDevices":[<device names>],"steps":[<steps>]}`.
`pairedDevices` may be absent (no device paired). No other key is allowed.

**Runners.** `runs` names the runners that run the fixture:

| `runs` value | Runner | Plays |
| --- | --- | --- |
| `"service"` | the service's (`rendezvous/tests/runner.py`), which runs every fixture | every connection, against the real service it builds from `serverSetup` |
| `"core"` | the Core's station role (Task 27) | the service, towards its Core on the connection named `station` |
| `"app"` | a client's (the desktop's, Task 27; the phone's, Task 27a) | the service, towards its client on the connection named `client` |

**Connections.** A step names a connection. A name starting `station` is a
station connection and one starting `client` is a client connection:
`station`, `station2`, `client`, `client2` and so on. A core runner plays
only `station`, an app runner only `client`; other connections exist only
in the service's run.

**Steps.** One of:

- `{"connect":"<conn>"}` or `{"connect":"<conn>","address":"<ip>"}`: the
  connection opens. The service's runner connects to the service and sends
  `address` (default `192.0.2.1`) as `X-Forwarded-For`, so fixtures can
  place connections on different networks. A core or app runner starts its
  client here, in the role the connection's name gives; the next behaviour
  step says what the client is asked to do.
- `{"from":"server","to":"<conn>","message":{...}}`: the service sends this
  to that connection. The service's runner matches it against the next
  message the connection received. A core or app runner sends it, filled,
  when `to` is its connection, and otherwise only records its placeholders.
- `{"from":"<conn>","role":"behaviour"|"scripted","message":{...}}`: the
  connection sends this. Roles mean what they mean in link section 16.3: a
  behaviour message is one the client under test must produce (a core or app
  runner drives its client to it and matches what it sends); a scripted one
  is a message a conformant client never sends, which a core or app runner
  skips while it gives its client the service's answers that follow. The
  service's runner sends both, filled.
- `{"advanceMs":N}`: time moves N ms. The service's runner waits until the
  service has read everything sent so far, then moves the service's clock,
  firing its timers in order. A core or app runner moves its client's clock
  if it keeps one.
- `{"disconnect":"<conn>"}`: that end closes its connection. A core or app
  runner drives its own client to close; for another connection it does
  nothing.
- `{"expectClosed":"<conn>"}`: the service has closed the connection, with
  no message between the last one listed and the close. A core or app
  runner closes its connection there (code 1000), and its client must send
  nothing after it.
- `{"expectSilent":"<conn>"}`: nothing more has arrived at that connection.
  The service's runner waits until the service has read everything sent and
  emptied its queues, then checks that nothing is waiting (a closed
  connection receives nothing, so it passes too). A core or app runner
  checks instead that its client sends nothing within 1 s of real time,
  whichever connection is named: that is how a Core's silence towards an
  unpaired device is held (`introduce-unpaired-device`).

A step holds no other key. The service's runner also decodes every message
the service sends with the receiving role's decoder, and fails on a key the
role does not know.

**`serverSetup`** holds the service's configuration for the fixture;
absent keys have these defaults:

| Key | Default |
| --- | --- |
| `stunUrls` | `["stun:rv6.conformance.invalid:3478","stun:rv4.conformance.invalid:3478"]` |
| `turnUrls` | `["turn:rv6.conformance.invalid:3478?transport=udp","turn:rv4.conformance.invalid:3478?transport=udp"]` |
| `turn` | `true`: a TURN secret is configured (the runner makes one at run time); `false`: none |
| `turnTtlSeconds` | 86400 |
| `wallClock` | 1800000000: the Unix time when the fixture starts; it moves only with `advanceMs` |
| `introductionsPerAddressPerMinute`, `introductionsPerStationPerMinute`, `mailboxOpensPerAddressPerMinute`, `candidatesPerSide`, `mailboxMessagesPerSide`, `connectionsPerAddress`, `maxConnections`, `handshakeTimeoutMs`, `idleTimeoutMs`, `introductionLifetimeMs`, `mailboxLifetimeMs` | section 9.1 |

A core or app runner reads `stunUrls`, `turnUrls`, `turnTtlSeconds` and
`wallClock` to fill the service's messages, and ignores the rest.

**`pairedDevices`** names the devices (as in `"$device:<d>:id"`) the Core
has paired before the fixture starts. The core runner makes their keys at
run time and pairs them into its Core by its own means; a device not listed
is not paired. The other runners ignore it.

**Placeholders.** A message may hold placeholders in place of values, each
a JSON string. Those of link section 16.1 that apply keep their meaning:
`"$any"`, `"$string"`, `"$string:<name>"`, `"$int"`, `"$int:<name>"`,
`"$capture:<name>"` and `"$ref:<name>"`, with names recorded once per run
and recorded again by a later placeholder of the same name. The rest are
the rendezvous's own. "Fill" is what a runner puts in a message it sends,
"match" what it checks in a message it receives; a runner also fills the
placeholders of a message on a connection it does not play, to record
them, without sending anything.

| Placeholder | Fill | Match |
| --- | --- | --- |
| `"$b64:<n>:<name>"` | `<n>` random bytes, base64url; recorded | strict base64url of exactly `<n>` bytes; recorded |
| `"$key:<k>:id"`, `"$key:<k>:publicKey"` | the rendezvous id, or the base64url SPKI, of station key `<k>` | equal to that value |
| `"$device:<d>:id"` | the device id (link section 3.5) of device key `<d>` | equal to that value |
| `"$register:<k>:<nonce>:<case>"` | a signature by station key `<k>` over the registration transcript of the nonce recorded as `<nonce>` (section 4.3); `<case>` is `signed`, `otherNonce` (over a random nonce instead) or `flippedBit` (the last bit of `s` flipped) | equal to the value filled for this placeholder |
| `"$introduce:<d>:<k>:<nonce>:<case>"` | a signature by device key `<d>` over the introduction transcript of station key `<k>`'s id and the nonce recorded as `<nonce>` (section 4.4); cases as above | equal to the value filled for this placeholder |
| `"$sdp:offer:<name>"`, `"$sdp:answer:<name>"` | the text of `sdp/offer.sdp` or `sdp/answer.sdp`; recorded | a non-empty string; recorded |
| `"$candidate:<name>"` | a host candidate line of the runner's own; recorded | a string; recorded |
| `"$turn:<k>:<name>"` | credentials minted as section 8 with the runner's own secret, for station key `<k>`, expiring at `wallClock` plus the seconds advanced plus `turnTtlSeconds`, with `turnUrls`; recorded | exactly that object: the username is `<expires>:<id of k>`, the password the HMAC-SHA1 of it under the secret the runner gave the service (computed by the runner, not the service), `urls` the configured list; recorded |

Keys and devices are named by short names (`a`, `b`, `d`, `e`) and made at
run time, never written in a fixture; a key name and a device name live in
separate namespaces. Where a runner does not make a key itself, it takes
it from its client:

- A **core runner**'s station key is its Core's identity key: at the
  behaviour `register`, `"$key:<k>:publicKey"` and `"$key:<k>:id"` are
  checks (a canonical P-256 key; the id derived from it) that record `<k>`
  as that key, and `"$register:<k>:<nonce>:signed"` in its `prove` is a
  check that the signature verifies. Every device key is the runner's own.
- An **app runner** makes each station key and gives its client the one
  its behaviour `introduce` names as the Core it paired with. Its client's
  device key is `<d>` wherever the client's own behaviour messages name
  `"$device:<d>:id"`: the runner takes the public key from its client and
  checks the id is its fingerprint, and checks that
  `"$introduce:<d>:<k>:<nonce>:signed"` verifies over this connection's
  transcript. `"$sdp:offer:<name>"` in its client's `introduce` records
  whatever offer the client made.

A behaviour step's literal values are what the runner tells its client to
send (a nameplate number, a mailbox body), as in link section 16.3.

**The fixtures.**

| Fixture | Runs | What it holds |
| --- | --- | --- |
| `register` | service, core | hello, register, challenge, a valid proof, `registered` |
| `register-other-nonce`, `register-flipped-bit`, `register-other-key` | service | a proof over another nonce, with a flipped bit, or by another key: `proofFailed`, the close |
| `register-id-mismatch`, `register-key-not-p256` | service | an id not derived from the key, and a 91-byte key whose point is not on P-256: `proofFailed` before any challenge, the close |
| `register-prove-first`, `register-twice`, `station-verbs-before-register` | service | out of turn: `protocolError`, the close |
| `register-replaced` | service | a second registration of the same key replaces the first (`replaced`, the close) and receives the next introduction; the first receives nothing |
| `register-timeout`, `handshake-timeout` | service | no proof within 10 s, and no first message within 10 s (nothing at 9999 ms): `timeout`, the close |
| `introduce-answer-relay` | service, core, app | introduce, the introduction with the client connection's nonce and the device fields untouched, an answer with relay, the same credentials to both ends |
| `introduce-answer-direct` | service, app | an answer without relay: `turn` null, no `credentials` |
| `introduce-relay-off` | service | relay asked for on a service without a secret: `turn` null to both |
| `introduce-unpaired-device` | service, core | an introduction from a device the Core has not paired: forwarded, and nothing comes back |
| `introduce-signature-other-nonce`, `introduce-signature-flipped-bit` | service, core | a signature over another nonce, or with a flipped bit: forwarded untouched, and the Core stays silent |
| `introduce-candidates` | service | candidates both ways, the end of candidates, and the cap (2 here) on each side |
| `introduce-station-candidate-before-answer` | service | `protocolError` for the station; the client gets `stationLeft` |
| `introduce-answer-twice`, `introduce-answer-unknown` | service | `unknownIntroduction`, the connection stays |
| `introduce-offline` | service, app | `offline` for an id never registered |
| `introduce-unknown-equals-offline` | service | the same answer for a station that left and an id never seen (the byte comparison is `test_authorisation.py`) |
| `introduce-only-own-id` | service | with two stations registered, the introduction reaches only its own |
| `introduce-twice` | service | a second `introduce` while one is live: `protocolError`; the station gets `clientLeft` |
| `introduce-client-leaves` | service, core | the client closes: `introduction.end` `clientLeft` |
| `introduce-station-leaves` | service, app | the station closes: `introduction.end` `stationLeft` |
| `introduction-expires` | service, core, app | nothing at 119999 ms; `expired` to both at 120000 ms |
| `client-candidate-without-introduction` | service | `noIntroduction` |
| `client-idle` | service | a client with nothing pending: nothing at 29999 ms, `idle` and the close at 30000 ms |
| `introduce-rate-limit-address`, `introduce-rate-limit-station` | service | the per-address limit (2 here) with its `retryAfterMs`, allowed again when the window passes; the per-station limit (1 here) across two addresses, counted for an id that is offline |
| `nameplate-claim` | service, core | claim 1, claim again (1), release, release again, claim 1 |
| `nameplate-lowest-free` | service | 1, 2, 3; 2 released and 1's station leaves; the next claim gets 1, and 2 again after it |
| `mailbox-exchange` | service, core, app | open, `mailbox.opened` to both, bodies both ways (one with line ends, quotes, a backslash and characters outside ASCII) forwarded unchanged, close, `closed` and `peerClosed` |
| `mailbox-station-closes` | service, app | the station closes the mailbox; another client opens the next one on the same nameplate |
| `mailbox-busy`, `mailbox-unknown` | service (`mailbox-unknown` also app) | `nameplateBusy` with the station told nothing; `nameplateUnknown` |
| `mailbox-station-leaves`, `mailbox-client-leaves` | service, and app or core | `peerLeft`; the freed nameplate serves the next station, or the next mailbox |
| `mailbox-release` | service, app | the station releases its nameplate with a mailbox open: `released` to the client |
| `mailbox-expires` | service | `expired` to both at 300000 ms |
| `mailbox-without-open`, `mailbox-message-cap`, `mailbox-rate-limit` | service | `noMailbox` from each side; `tooManyMessages` at the cap (1 here); the per-address open limit (1 here) |
| `unknown-kind`, `client-claims-nameplate`, `station-introduces` | service | an unknown kind, and each role sending the other's: `protocolError`, the close |
| `connections-per-address`, `connections-per-ipv6-prefix` | service | the per-address cap (1 here) answered with `tooManyConnections` instead of `hello`; an IPv6 /64 counted as one address, another /64 not; an IPv4-mapped address counted as its IPv4 address |

### 10.5 Running the service's runner

```
python3 -m pytest rendezvous/tests -q
```

The authoritative run is inside an `ubuntu:24.04` container with Ubuntu's
`python3-websockets`, `python3-cryptography` and `python3-pytest`, which is
also what CI installs (the `rendezvous` job). The service runs on websockets
10.4 (Ubuntu 24.04, the legacy asyncio implementation) and on websockets 13
and later (the new one); `rendezvous/server/nereus_rendezvous/transport.py`
isolates the difference. The runner also alters one fixture in memory and
checks that the failure names the step and field that differ.

## 11. Changing the rendezvous

- **The document, the vectors and the service change together**, in the same
  commit, and the service's runner passes. A change to a kind the Core or a
  client uses reaches their runners (Tasks 27 and 27a) before it ships.
- **Versions add, never change.** A later version adds kinds and keys and
  raises `hello` `version`; it never changes or removes what version 1
  defines, and `offline`'s bytes never change. A peer sends a kind or key
  added after version 1 only to a service whose `hello` `version` includes
  it, because an older service ends the connection on a kind it cannot
  decode (a key it does not know it ignores).
- **Nothing on disk, nothing sensitive in the log** (section 9) is part of
  the wire's contract, not an implementation detail: a self-hoster relies
  on it.
