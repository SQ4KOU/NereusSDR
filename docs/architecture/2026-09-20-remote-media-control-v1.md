# Remote media control version 1

This is the R3 implementation contract for R-R3-02 through R-R3-09.
Session protocol minor 1 and capability `remoteMediaVersion=1` negotiate it.
An authenticated, snapshot-complete active control session is required in
both directions. Older clients retain the R2 control protocol.

Control messages use the existing WSS envelope:

```json
{"type":"media.control","payload":{"op":"start","connectionId":"canonical-uuid"}}
```

The complete encoded envelope is limited to 128 KiB. Each operation has an
exact set of keys. Integers are JSON numbers checked for range and integrality
before narrowing; strings and numeric values are never coerced. Display
arrays and audio packets use the separate encrypted media connection.

## Media peer

`connectionId` is a non-null lowercase UUID with hyphens and no braces.
The GUI creates it after the control snapshot and sends `start`; Core is
the offerer and the GUI the answerer. Every subsequent operation carries
the same ID. Control-session replacement retires the old peer, subscriptions,
codec histories and callbacks, including deliberate silent redials.

| Operation | Exact payload fields beyond `op` and `connectionId` |
| --- | --- |
| `start` | None; a GUI whose Core advertised `audioProfileVersion` adds `audioProfileVersion`, a whole number of at least 1 (anything else is refused and no peer starts); a GUI whose Core advertised `receiverAudioVersion` may add `receiverAudioVersion` the same way (see Receiver audio), one whose Core advertised `headphonesMixVersion` may add `headphonesMixVersion` the same way (see Headphones mix), and one whose Core advertised `remoteTxVersion` may add `remoteTxVersion` the same way (see Microphone line) |
| `description` | `sdp`, `type` (`offer` or `answer`, appropriate to peer role) |
| `candidate` | `candidate`, `mid` |

SDP is limited to 64 KiB. Candidate and MID strings are limited to 4 KiB and
256 bytes respectively; NUL is forbidden. At most 64 remote candidate
controls are admitted, including candidates buffered before the description.
The R3 backend accepts host ICE candidates. SDP-embedded candidates are
rejected: this version uses bounded trickle candidates exclusively. This
provides direct LAN media,
and does not close the separate R5 TURN/TCP, TURN/TLS or NAT traversal gates.

Display is an unordered SCTP data channel with zero retransmissions, secured
by DTLS. Audio is separate RTP/SRTP, payload type 111. The session audio SSRC
is derived by SHA-256 over the ASCII prefix `NereusSDR/media-audio-ssrc/v1:`
followed by the canonical connection UUID. The first four digest bytes form
an unsigned big-endian integer; zero maps to one. Both peers use this ID,
and reject mismatching RTP. SSRC is a routing identity; authentication comes
from the pinned WSS session and its negotiated DTLS peer.

Receiver audio streams (R-R3-43) share the one audio m-line and its SRTP
context with the main stream; only their SSRCs differ. Receiver stream `n`,
for `n` from 0 to 3, has the SSRC formed the same way from the ASCII prefix
`NereusSDR/media-receiver-ssrc/v1:`, the decimal `n`, a colon, then the
canonical connection UUID. A value that is zero, equal to the main SSRC or
equal to an earlier receiver's SSRC is replaced by the next integer (modulo
2^32) until it is none of these, so the five IDs are distinct and both peers
derive the same set. The receiver streams are declared only when both peers
start the media connection with receiver audio (the GUI added
`receiverAudioVersion` to its `start`; see Receiver audio).
Then Core's offer carries, after the main stream's
`a=ssrc:<main> cname:nereus-mixed-stereo` line, one
`a=ssrc:<receiver n> cname:nereus-receiver-<n>` line per receiver in order,
and each peer sends and accepts exactly the main SSRC and the four receiver
SSRCs. Without it the offer, the answer and every audio line are exactly as
before, and only the main SSRC is sent or accepted. RTP with any other SSRC
is refused and reported. The transport library itself does not refuse it:
libdatachannel v0.24.5 hands every packet on a connection with one media
line to that line's track whatever its SSRC (measured, not assumed), so the
refusal is the media peer's receive filter. Each peer's queue of received
RTP between drains holds 64 packets per declared stream, 256 ms of lossless
audio (250 packets/s) for every stream: 64 packets without receiver
streams, 320 with them, and 64 more with the headphones mix; when full, the
oldest packet is dropped.

The headphones mix (R-R3-45) is one more stream on the same m-line and SRTP
context. Its SSRC is formed the same way from the ASCII prefix
`NereusSDR/media-headphones-ssrc/v1:` followed by the canonical connection
UUID; a value that is zero, the main SSRC or any of the four receiver SSRCs
(declared or not) is replaced by the next integer (modulo 2^32) until it is
none of these. It is declared only when the GUI added
`headphonesMixVersion` to its `start`: Core's offer then carries, after any
receiver lines, one `a=ssrc:<headphones> cname:nereus-headphones-mix` line,
and each peer also sends and accepts that SSRC. Without it nothing above
changes.

The microphone line (iPhone app plan Task 36) is a second audio m-line,
`a=mid:mic`, after the main one, which the client sends on and the Core
receives. It is offered only when the client added `remoteTxVersion` to
its `start` (see Microphone line); without it the offer, the answer and
every audio line are exactly as before. Core's offer declares it
receive-only (`a=recvonly`), with no SSRC of the Core's, carrying Opus as
`a=rtpmap:111 opus/48000/2` with
`a=fmtp:111 minptime=10;useinbandfec=1;stereo=0;maxaveragebitrate=24000`,
and, when the offer carries the lossless format on the main line, the same
`a=rtpmap:96 L16/48000/2` after Opus. The answer takes it send-only and
declares the microphone's SSRC on it:
`a=ssrc:<microphone> cname:nereus-microphone`. That SSRC is formed from the
ASCII prefix `NereusSDR/media-mic-ssrc/v1:` followed by the canonical
connection UUID; a value that is zero, the main SSRC, any of the four
receiver SSRCs or the headphones SSRC (declared or not) is replaced by the
next integer (modulo 2^32) until it is none of these. The client sends only
that SSRC on the line and the Core accepts only it; the line joins the
bundle and the lip-sync group (`a=group:BUNDLE audio mic 0`,
`a=group:LS audio mic`). The Core's queue of received RTP holds 64 more
packets for it.

## Display subscriptions

GUI-to-Core `subscribe` has these exact additional fields:

```text
endpointId, revision, sliceId, tier, fftSize, windowType,
centreHz, spanHz, pixels, fps, framesPerLine,
trace, waterfall, minDbm, maxDbm, wideSpanFactor
```

`endpointId` and `revision` are nonzero uint32 values. Revisions use unsigned
half-range ordering across wrap. `sliceId` must identify a live station
slice; Core resolves its stream, so the client cannot request an arbitrary
DDC. `tier` is `wide` or `fine`. Up to eight endpoints are admitted, each
requesting 1..4096 pixels and 1..60 frames/second. An FFT size is a power of
two from 1024; a size FFTEngine supports is honoured as asked, and a larger
one is granted FFTEngine's largest size. A request sizes its (stream, tier)
engine only while it is that engine's only subscriber; beside another
endpoint it is granted the engine's current size, so no pan's spectrum
changes to satisfy another's request. When the last neighbour leaves, an
endpoint held to a neighbour's size is granted its own. The engine runs at
the highest frame rate its endpoints ask for, and each endpoint keeps its own
cadence, so a rate change alone renews no endpoint. Pixels are granted as
min(requested, visible source bins, 4096). Incompatible window choices are
refused. A global
window change unsubscribes all old-window endpoints before requesting any
replacement, so shared sources can adopt the new window.

Both `trace` and `waterfall` contain exactly `detector`, `averageMode` and
`averageAlpha`. Detector codes are the existing SpectrumDetectorMode values:
0 peak, 1 Rosenfell, 2 average, 3 sample, 4 RMS. Averaging codes are the
existing SpectrumAvenger values: -1 peak hold, 0 none, 1 recursive linear,
2 time window linear, 3 recursive logarithmic. Alpha is finite in [0,1].
Each plane owns an independent reducer history.

Frequencies and spans are in Hz. Spans must be positive; source and request
edges and the requested wide product must remain finite. The quantization
window has finite `minDbm < maxDbm`. `wideSpanFactor=0` disables wide coverage;
otherwise it exceeds one and represents the GUI's maximum 3D shape. Core
clamps coverage to the actual source and the wide row to 768 samples. A
wholly nonoverlapping request is rejected. A retune that removes its coverage
also rejects and retires the endpoint; Core never acknowledges a zero-span
display context.

Core-to-GUI `context` has exactly 19 fields in total:

```text
op, connectionId, endpointId, revision, contextGeneration, sourceStream,
sourceCentreHz, sampleRateHz, centreHz, spanHz, wideCentreHz, wideSpanHz,
traceSamples, waterfallSamples, wideSamples, minDbm, maxDbm, fps, framesPerLine
```

A subscription that negotiated the extended view (minor 6) adds a 20th
field, `wideband`.

Session protocol minor 9 and capability `spectrumGrantVersion=1` add five
fields reporting what Core granted the endpoint (R-R3-01, R-R3-08), so the
context has 24 fields, or 25 with `wideband`:

```text
grantedFftSize, grantedTier, requestedPixels, grantedPixels, limit
```

`grantedFftSize` is the FFT size the endpoint's engine actually runs
(1..262144). `grantedTier` is `wide` or `fine`. `requestedPixels` and
`grantedPixels` are 1..4096 with granted not above requested. `limit` names
what reduced the grant: `none`, `largest-size` (the request was above the
largest supported FFT size), `shared` (another endpoint uses the same stream
and tier engine, so its size stands) or `source-bins` (the crop has fewer
source bins than the requested pixels). A minor 8 or older peer receives
the 19- or 20-field context unchanged, and each side accepts only the shape
it negotiated. Both sides use one codec, `RemoteSpectrumContext`. The GUI
shows a plain status line on the pan while the grant is limited.

Core configures this from the first actual frame of the current source
generation, then sends it before encoded display. Exact sample counts clamp
to available cropped bins. Center/span describe the accepted bin-aligned
coverage; they are acknowledgments, not a new GUI gesture. A retune or
reconfiguration creates a new context generation and clears input overlap,
reduction history and pending output. A frame arriving before its context
is discarded; the GUI requests a keyframe after accepting context.

| Operation | Exact additional fields |
| --- | --- |
| `unsubscribe` | `endpointId` (and `revision` on the display budget wire, below) |
| `keyframe` | `endpointId`, `contextGeneration` |
| `rejected` (Core to GUI) | `endpointId`, `revision`, `reason` |
| `noise-floor` (Core to GUI) | `endpointId`, `revision`, `contextGeneration`, `floorDbm` |

R-R3-12 adds `noise-floor` as optional display metadata; older clients ignore
the unknown operation. It has exactly six payload fields including `op` and
`connectionId`. Core sends it after the accepted context, once initially and
then at most twice per second per endpoint, using a fresh source frame.
`floorDbm` is finite and bounded to [-400,100]. It is the existing
NoiseFloorEstimator's 30th percentile of the full-source FFT dBm bins, with
station calibration applied once, before viewport crop, detector, averaging
or codec quantization. No full FFT array travels over WSS.

The GUI requires the current authenticated session, connection ID, endpoint
revision and context generation. Only the visible active pan with unchanged
subscription inputs may feed its existing Clarity controller. Local cadence,
EWMA, deadband, manual override, re-tune, TX pause and palette behavior remain
in effect. The binary display codec and R3 media version are unchanged. This
restores the existing global active-pan Clarity behavior; independent
per-pan Clarity controllers and Auto AGC-T telemetry remain separate work.

Keyframe requests are limited to five per endpoint per second and must
match its current context. A whole-peer rejection uses endpoint/revision
zero. Unsubscribe releases an unused source. Rebinding, slice removal and
disconnect invalidate the corresponding endpoint. Hidden GUI panes
unsubscribe, and a newly shown pane receives a new endpoint ID.

### Display budget (unsubscribe revision and allocation-result)

When the Core enforces a session display budget it advertises capability
`remoteDisplayBudgetVersion=1`, and a peer at session protocol minor 7 or
later (`kRemoteDisplayBudgetSessionProtocolMinor`) gets the budget wire
below. The Core applies it while media is available, budget enforcement is
on and a budget is in force for the session (`StationServer::
displayBudgetAvailable`). A budget the Core computed for itself is in
force only for a peer at minor 11 (`kDisplayBudgetReasonSessionProtocolMinor`);
an older peer keeps the legacy wire exactly: no budget, no pacing, no
allocation results. `DaemonMediaController::handleUnsubscribe` and
`sendAllocationResult` are the code.

On the budget wire, GUI-to-Core `unsubscribe` has exactly `op`,
`connectionId`, `endpointId` and `revision`: the endpoint's release is a
revisioned operation like a subscribe, and `revision` is a nonzero uint32.
Without the budget wire it has exactly `op`, `connectionId` and
`endpointId`, as in the table above.

Core-to-GUI `allocation-result` answers every subscribe and unsubscribe
outcome on the budget wire, and replaces `rejected` for an endpoint with a
nonzero endpoint and revision there (an involuntary retirement, such as a
source retune or slice removal, is reported the same way, so the GUI learns
the charge the Core released). It has exactly 11 fields:

```text
op, connectionId, endpointId, revision, accepted, reason, budgetGeneration,
acceptedRevision, applicationBytesPerSecond, spectrumSampleUnitsPerSecond,
messagesPerSecond
```

| Field | Meaning |
| --- | --- |
| `op` | `allocation-result` |
| `connectionId` | the media peer's connection ID |
| `endpointId` | the endpoint the operation named; the desktop client accepts 1 to 4294967295 |
| `revision` | the revision of the subscribe or unsubscribe this answers; 1 to 4294967295 |
| `accepted` | boolean: whether that operation was granted |
| `reason` | empty when accepted; otherwise why not, in plain words (the link document's section 17); the desktop client reads at most 512 characters |
| `budgetGeneration` | the generation of the display budget the Core judged it against (`DisplayBudgetLimits::generation`); 1 to 4294967295 |
| `acceptedRevision` | the revision the Core now holds for the endpoint, or 0 when it holds none; 0 to 4294967295 |
| `applicationBytesPerSecond`, `spectrumSampleUnitsPerSecond` | the charge the Core retains for the endpoint after this outcome; whole numbers up to 9007199254740991 |
| `messagesPerSecond` | the retained message rate; 0 to 200 (`kDisplaySenderMessagesPerSecond`, one per 5 ms sender interval) |

The desktop client refuses a result unless the three charge fields are
all 0 when `acceptedRevision` is 0 and all above 0 when it is not, and
unless it has exactly these 11 fields. A result whose `revision` is not the GUI's
pending one only restates what the Core retains: the desktop client
applies it only when it releases a reservation (acceptedRevision 0, zero
charge) for the endpoint's current revision.

The [display codec specification](2026-09-20-display-codec-v1.md) defines
the binary packets, reconstruction and loss recovery. Pending source/output
slots and transport queues are bounded. A failed nonblocking send is not
retried with the same bytes: it may already have entered the library buffer;
the following attempt uses a keyframe. Dropped I/Q invalidates the FFT input
history before post-gap samples are processed. Endpoint cadence follows an
advancing schedule with bounded early-jitter tolerance; it does not restart
its entire interval after each arrival or catch up with a burst after a stall.

## Receiver audio (receiver-audio and receiver-audio-context)

Capability `receiverAudioVersion=1` (R-R3-43) negotiates it; the session
protocol minor is unchanged. The Core advertises version 1 whenever media is
on. A GUI that sees it may add `receiverAudioVersion` (a whole number of at
least 1) to its `start`; only a GUI at the audio status detail minor may, and
a malformed value starts no peer. Only then does the offer declare the four
receiver stream IDs (see Media peer), and only then does the Core honour a
`receiver-audio` request. A GUI that did not declare it at `start` is never
sent a receiver stream or a receiver context, whatever it asks: the transport
library would deliver such packets to it and it would refuse and report each
one. Its offer, contexts and packets are exactly as before.

Each stream is one receiver's own audio: 48 kHz stereo taken where local VAX
takes it, after the transmit gate and before the slice's mute, gain and pan,
the mix and the speakers' volume, with that slice's AF gain undone as local
VAX undoes it. While the transmit gate withholds the slice's audio the stream
sends nothing and its RTP timestamps advance over the gap. A receiver stream
runs beside the main one; starting, stopping or changing it never restarts or
re-announces the main stream, and the main `audio` control never touches a
receiver stream.

GUI-to-Core `receiver-audio` has exactly these fields:

| Field | Meaning |
| --- | --- |
| `op`, `connectionId` | As every operation; the current peer's ID |
| `sliceId` | Non-negative integer, the Core's slice ID |
| `revision` | Nonzero uint32; per slice ID for the whole media connection, it only goes up (serial-number order, as `audio`) |
| `enabled` | Boolean |
| `profile` | `opus` or `lossless`, the session's one audio quality choice |

The Core ignores a request with any other key, a wrong or retired
`connectionId`, a malformed field, or a revision at or below the slice's last
accepted one. Each accepted request, and each change of radio, media
readiness or slice that affects a wanted stream, is answered with one
`receiver-audio-context`: the audio-profile shape of `audio-context` with op
`receiver-audio-context` and `sliceId` added:

| Field | Meaning |
| --- | --- |
| `op` | `receiver-audio-context` |
| `connectionId`, `revision`, `enabled` | As `audio-context`; `revision` is the slice's request it answers |
| `sliceId` | The slice the stream carries |
| `generation` | uint32 counting receiver contexts; its own count, not the main context's generation |
| `ssrc` | The receiver stream ID the packets carry, or 0 when a disabled context holds none (`receiver-limit`, or a slice that was never there) |
| `firstSequence`, `firstTimestamp` | Where the stream ID's RTP timeline continues; each receiver stream ID keeps one timeline for the whole media connection, as the main stream does; 0 with `ssrc` 0 |
| `profile`, `encoder`, `profileRefusal` | As the audio-profile shape of `audio-context` |
| `reason` | While disabled: `client-disabled`, `media-not-ready`, `radio-offline`, `encoder-unavailable`, `slice-removed` or `receiver-limit` |

The profile follows the rules of the main stream: Opus, or lossless when
asked, allowed by the Core's `audio_lossless` setting and carried by this
media connection; otherwise Opus with `profileRefusal`. Opus on a receiver
stream always runs at 48000 bit/s with fullband sound (audio up to 20 kHz),
whatever the Core's `audio_bitrate` (which sets the speakers' mix and the
headphones mix only): when Opus is asked for, when lossless is refused and
when the GUI asks for Opus after its link trial (operator decision of
2026-09-24, from the FT8 measurement in
`2026-09-20-remote-daemon-r3-verification/digital-modes-over-opus.md`). The
`encoder` object reports it, so a GUI reads the rate from the context and
assumes none; the media offer's `maxaveragebitrate` stays the main stream's
target, as before. The GUI keeps every stream on the one choice, and one link
trial covers every lossless stream.

At most four receiver streams run at once, one per receiver stream ID; the
lowest free ID is taken when a stream is wanted and kept until the stream is
turned off, its slice goes or the session ends. A fifth request is refused
with `receiver-limit` and `ssrc` 0 and is not queued: the GUI asks again once
it has let a stream go. A wanted stream keeps its ID and intent while the
radio is offline or the media connection is not ready, and resumes when both
return. When the slice is removed its stream stops with `slice-removed`, its
ID goes free, and its revision stays so a stale request stays refused; a
request for a slice ID the Core does not have is answered `slice-removed`
and remembered nowhere. When the session or the media connection ends every
receiver stream stops with it, with no context (there is no GUI to tell).

The two new reason strings, `slice-removed` and `receiver-limit`, occur only
in `receiver-audio-context`; an `audio-context` carrying one is malformed. A
GUI shows every reason through `OperatorReasonText` in plain words, never as
the wire string.

## Headphones mix (headphones-audio and headphones-audio-context)

Capability `headphonesMixVersion=1` (R-R3-45) negotiates it; the session
protocol minor is unchanged. The Core advertises version 1 whenever media is
on. A GUI that sees it may add `headphonesMixVersion` (a whole number of at
least 1) to its `start`; only a GUI at the audio status detail minor may,
and a malformed value starts no peer. Only then does the offer declare the
headphones stream ID (see Media peer) and does the Core honour a
`headphones-audio` request. A GUI that did not declare it gets exactly the
wire it gets today: no headphones ID, no headphones context, and the main
stream carrying the station's whole program.

Each slice has a speakers-or-headphones output route (`outputRoute`, a
mirrored slice property both sides may write, saved and restored by the
Core). The Core's mixer makes two mixes from the one set of receivers, each
at its slice's gain, pan and mute: the speakers' mix (the receivers routed
to the speakers) and the headphones mix (those routed to the headphones).
For a GUI that declared the headphones mix, the main stream carries the
speakers' mix alone, and the headphones mix travels on its own stream
while it runs; a receiver routed to the headphones is heard only there.
For any other GUI the main stream carries both mixes added together, as
before. Neither mix carries master volume or mute; those are the GUI's
own, on its speakers only.

GUI-to-Core `headphones-audio` has exactly these fields:

| Field | Meaning |
| --- | --- |
| `op`, `connectionId` | As every operation; the current peer's ID |
| `revision` | Nonzero uint32; for the whole media connection it only goes up (serial-number order, as `audio`) |
| `enabled` | Boolean: whether this computer can play the headphones mix now (it has headphones open and they have not failed) |
| `profile` | `opus` or `lossless`, the session's one audio quality choice |

The Core ignores a request with any other key, a wrong or retired
`connectionId`, a malformed field, or a revision at or below its last
accepted one. The headphones mix runs while the GUI asked for it, some slice
is routed to the headphones, the radio is connected and media is ready.
Each accepted request, and each change of those that starts or stops the
mix, is answered with one `headphones-audio-context` (a radio drop only when
it changes what the app was last told: a mix that was sending stops, and
`media-not-ready` becomes `radio-offline`, while `no-headphones-receiver`
and `client-disabled` stand): the audio-profile
shape of `audio-context` with op `headphones-audio-context`:

| Field | Meaning |
| --- | --- |
| `op` | `headphones-audio-context` |
| `connectionId`, `revision`, `enabled` | As `audio-context`; `revision` is the newest `headphones-audio` request |
| `generation` | uint32 counting headphones contexts; its own count |
| `ssrc` | The headphones stream ID, always |
| `firstSequence`, `firstTimestamp` | Where the headphones stream's RTP timeline continues; it keeps one timeline for the whole media connection |
| `profile`, `encoder`, `profileRefusal` | As the audio-profile shape of `audio-context` |
| `reason` | While disabled: `client-disabled`, `no-headphones-receiver`, `radio-offline`, `media-not-ready` or `encoder-unavailable` |

Routing a second or third receiver to the headphones, or one of several
back, changes only what the mix contains, not the stream: no new context
is sent. The profile follows the rules of the main stream, and the GUI's
one link trial counts the headphones mix beside every other lossless
stream, so one fallback moves it to Opus with the rest. Starting, stopping
or changing the headphones mix never restarts the main stream or a
receiver stream. When the session or the media connection ends the
headphones mix stops with it, with no context.

The GUI plays the headphones mix on this computer's headphones output with
its own receiver and rate matcher, paced by that device's clock. A
headphones device failure stops only that receiver: the GUI asks the Core
to stop the mix, says what happened in plain words, and the speakers play
on. It asks again when the headphones device is opened, closed or
changed, or the operator chooses the audio quality again (a decoder that
could not start depends on it); a media reconnect keeps the failure.
With a Core that did not advertise the capability, a slice flag routed to
the headphones says in plain words that this Core cannot send audio for the
headphones, so the receiver plays on the speakers (such a Core sums every
receiver into the main stream).

The new reason string `no-headphones-receiver` occurs only in
`headphones-audio-context`; an `audio-context` or `receiver-audio-context`
carrying it is malformed. A GUI shows reasons through `OperatorReasonText`
in plain words, never as the wire string.

## Microphone line (iPhone app plan Task 36)

Capability `remoteTxVersion=1` (R-IOS-13), which the Core sends only to a
client whose hello declared `remoteTx` 1 at minor 11 (the link document,
section 18), negotiates it; the session protocol minor is unchanged. Such a
client may add `remoteTxVersion` (a whole number of at least 1) to its
`start`; from any other client, or malformed, the start is refused and no
peer starts. Only then does the offer carry the microphone line (see Media
peer). There is no control operation for the line: the client sends on it
when it transmits, and the Core decides what the transmitter takes.

**What the client sends.** The microphone, mono 48 kHz, as Opus 20 ms
frames with in-band FEC (payload type 111, one channel), or, from a desktop
remote window whose operator chose lossless audio and whose line agreed
the L16 format, as L16 packets of 192 frames with the microphone in both
channels (payload type 96). A client sends while its device holds transmit,
while its own key is down, or while it has VOX armed (the Core's VOX on and
its session permitted to transmit), and never otherwise. A program keying
through a desktop window's TCI server is sent on the line in place of the
microphone while its audio comes (the app's left channel, resampled to 48
kHz).

**What the Core does with it.** Packets are put in sequence order; one
behind the stream (reordered too late, or repeated) is dropped and counted.
A gap is rebuilt when the next packet arrives: every lost packet but the
last by Opus loss concealment, the last from the arriving packet's in-band
FEC (Opus codes FEC only for frames its voice detector calls active; for
any other frame the decoder conceals); a lost L16 packet is 4 ms of
silence. A gap longer than 60 ms inserts nothing. The decoded audio goes to
the transmit jitter buffer, WDSP rmatch run on the transmit pump's thread,
which matches the sender's clock to the radio's in both directions: its
ring targets 60 ms (rmatch holds it half full) and never exceeds 120 ms
(beyond that the oldest audio is dropped and counted). After every change
of use the buffer starts empty and the pump hears silence until it holds
60 ms, then the audio.

**When the transmitter takes it.** The transmitter's microphone is the line
while the device whose media carries it is keyed (its own key, its
program's key, or a VOX key that is its), while its key waits for the
buffer, and while it has VOX armed; otherwise the operator's configured
source applies. Every change empties the buffer, so at unkey nothing of the
device's audio is left. In the RADE modes the line feeds the RADE encoder
as any microphone does.

**Keying on a filled buffer.** A `tx.key` from a device whose media carries
the line, in a mode that transmits the microphone (every mode but CWL and
CWU), keys once the line's buffer holds its 60 ms target; if it has not
within 250 ms it is refused `micNotReady` (the link document, sections 18.3
and 18.6). TUNE and two-tone use no microphone and key at once, and a
device without the line keys with the Core's own source, as before.

**Starvation.** While the device is keyed on its line, 250 ms without audio
is starvation, and audio arriving again ends it. What the Core then does
depends on the transmit mode (iPhone app plan Task 37; the link document,
section 18.7): in LSB, USB, DSB, CWL, CWU, DIGL, DIGU and SPEC the key goes
on, silent; in AM, SAM, FM, DRM, RADE_U and RADE_L the Core stops
transmitting with "No microphone audio arrived from <device>, so the Core
stopped transmitting." TUNE and two-tone are never stopped by it.

**The "tx" data channel (iPhone app plan Task 37).** A media connection
whose `start` carried `remoteTxVersion` also has a second SCTP data
channel, labelled `tx`, which the Core (the offerer) opens beside
`display`, like it unordered and with zero retransmissions, so a lost
message is overtaken by the next instead of holding anything behind it.
The answerer takes it only when its own start asked for the line; any
other connection has only `display`, exactly as before. The device sends
its transmit keepalive on it, one binary message of 13 bytes every 100 ms
while it is keyed or has VOX armed: byte 0 is 1 (a keepalive), bytes 1
to 8 the `sequence` and bytes 9 to 12 the `epoch`, both big-endian, with
the meanings of `tx.keepalive` (the link document, section 18.7). The Core
reads anything else on it as nothing. While the channel is open the device
sends its keepalives there and not on the session; the Core counts either,
and the same sequence twice once.

**The monitor.** With MON on, the audio the Core sends a device while it
is keyed carries the transmit monitor in the Core's mix, exactly as the
Core's own speakers would play it (the monitor level, before the speakers'
volume). The phone plays it on headphones only.

## Measured audio delay (clock-probe and clock-echo)

Capability `audioClockVersion=1` (R-R3-35) negotiates it; the session
protocol minor is unchanged. The Core advertises version 1 whenever media is
on. A GUI sends probes only while audio plays to a Core that advertised it,
one every 1000 ms (`RemoteMediaController::kClockProbeIntervalMs`); a Core
without it is never probed and shows no delay.

GUI-to-Core `clock-probe` has exactly these fields:

| Field | Meaning |
| --- | --- |
| `op`, `connectionId` | As every operation; the current peer's ID |
| `id` | uint32, the probe's number |
| `t0` | Non-negative integer nanoseconds on the GUI's clock when the probe was sent |

Core-to-GUI `clock-echo` has exactly nine fields:

| Field | Meaning |
| --- | --- |
| `op`, `connectionId` | As every operation |
| `id`, `t0` | Copied from the probe |
| `t1` | Core clock (non-negative integer nanoseconds) when the probe arrived, read first |
| `t2` | Core clock when the echo left, read last |
| `generation` | The running audio context's generation, or 0 while no context is capturing |
| `rtpTimestamp` | The RTP time at the end of the newest captured block (its timestamp plus 1920), or 0 |
| `capturedNs` | The Core clock when that block's last frame reached the audio tap, or 0 |

The Core ignores a probe with any other key, a wrong or retired
`connectionId`, an `id` that is not a uint32, or a negative or non-integer
`t0`. The GUI accepts an echo only as the answer to one of its last eight
probes, matching both `id` and `t0`; `t3` is its own clock on arrival.

From the echo the GUI computes the Core-minus-GUI clock offset
`((t1 - t0) + (t2 - t3)) / 2` and the round trip `(t3 - t0) - (t2 - t1)`. Of
the probes in the last 16 s it uses the one with the lowest round trip; half
that round trip, plus a 100 ppm allowance for the two clocks' drift, bounds
the offset's error. Half the round trip is shown only as accuracy, never as
a delay. No figure is shown once the newest echo is 3 s old, while the
echo's `generation` is 0 or is not the context this computer plays, or across
a new audio context until its first echo arrives.

The delay is the time this computer plays a sample minus the time the Core
captured it (mapped through the offset). The play time counts, after the
rate matcher fill and the speaker queue, the fixed delays inside the
pipeline: the Opus codec's algorithmic delay (`OPUS_GET_LOOKAHEAD`, 312
frames at 48 kHz, for Opus only) and the rate matcher's filter delay (69
frames). The speaker queue drains a device callback at a time, so half a
callback is counted and the other half is added to the accuracy, together
with half the time the queue read took. The device's own latency is added
when the audio backend reports it; otherwise the figure says it does not
count the speaker device.

The figure is the delay of the sample heard when the queue was read, not of
the newest sample behind it. While the rate matcher corrects its fill, its
ratio (output frames made per input frame, WDSP rmatch's `var`) is not 1 and
the delay itself changes as the audio plays: the audio between the two
samples was made at that ratio, so it spans `1 / ratio` as much capture time
as play time. The newest sample's delay is reduced by that stretch, the
play time the rate matcher made (everything ahead of the newest sample but
the codec's delay) times `1 - 1 / ratio`, using the ratio at the reading.

The audio enable/context lifecycle, playback buffering and adaptive session
budget are still being implemented. Their acceptance remains open in the
[R3 plan](2026-09-20-remote-daemon-r3-plan.md); this document does not claim
live hardware or internet traversal acceptance.
